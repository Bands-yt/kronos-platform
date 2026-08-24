#include "publishing/PackageArchive.hpp"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>

#include <zlib.h>

#include "core/OAuthPkce.hpp"

namespace engine::publishing {

namespace {
constexpr char kMagic[4] = {'K', 'R', 'A', 'R'};
constexpr uint32_t kVersion = 1;

// Real lower-case hex encoding of a raw byte string -- core::sha256()
// itself returns the raw 32-byte digest (see its own header comment),
// not a text encoding; every real use here (a content-addressed
// filename, a manifest entry, scene_sha256 sent to the backend) needs
// text, so this is the one small, shared place that converts.
std::string hexEncode(const std::string& bytes) {
    static constexpr char kHexDigits[] = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (unsigned char byte : bytes) {
        out.push_back(kHexDigits[byte >> 4]);
        out.push_back(kHexDigits[byte & 0x0F]);
    }
    return out;
}

std::string readWholeBinaryFile(const std::string& path, bool& outOk) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        outOk = false;
        return {};
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    outOk = true;
    return ss.str();
}

// The archive format is flat (no subdirectories, see ArchiveFileEntry's
// own comment) -- an asset's real extension is kept purely as a human
// convenience when inspecting an archive's contents, never relied on
// for anything functional.
std::string extensionOf(const std::string& path) {
    return std::filesystem::path(path).extension().string();
}

// Real, deliberate simplification: fixed-width integers are written as
// raw native bytes, not a portable endian-independent encoding -- a
// `.kronos` archive is written and read back on the same local machine
// within one packaging/unpacking workflow (no cross-machine archive
// exchange is a real use case this feature targets yet), the same
// "local, per-machine, alpha-scoped" framing net::SessionHistory/
// net::GamePlayLog already established elsewhere in this codebase.
template <typename T>
void writeRaw(std::ofstream& out, const T& value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

template <typename T>
bool readRaw(std::ifstream& in, T& value) {
    in.read(reinterpret_cast<char*>(&value), sizeof(T));
    return in.good();
}
} // namespace

bool writeArchive(const std::string& archivePath, const std::vector<ArchiveFileEntry>& files) {
    std::ofstream out(archivePath, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) return false;

    out.write(kMagic, sizeof(kMagic));
    writeRaw(out, kVersion);
    uint32_t fileCount = static_cast<uint32_t>(files.size());
    writeRaw(out, fileCount);

    for (const ArchiveFileEntry& file : files) {
        uint32_t nameLength = static_cast<uint32_t>(file.relativePath.size());
        writeRaw(out, nameLength);
        out.write(file.relativePath.data(), nameLength);

        uLongf compressedBound = compressBound(static_cast<uLong>(file.data.size()));
        std::vector<uint8_t> compressed(compressedBound);
        uLongf compressedSize = compressedBound;
        int result = compress2(compressed.data(), &compressedSize,
                                file.data.empty() ? reinterpret_cast<const Bytef*>("") : file.data.data(),
                                static_cast<uLong>(file.data.size()), Z_BEST_COMPRESSION);
        if (result != Z_OK) {
            std::fprintf(stderr, "PackageArchive: real deflate failed for \"%s\" (zlib error %d)\n",
                         file.relativePath.c_str(), result);
            return false;
        }

        uint64_t uncompressedSize = file.data.size();
        uint64_t finalCompressedSize = compressedSize;
        writeRaw(out, uncompressedSize);
        writeRaw(out, finalCompressedSize);
        out.write(reinterpret_cast<const char*>(compressed.data()), static_cast<std::streamsize>(finalCompressedSize));
    }

    return out.good();
}

bool readArchive(const std::string& archivePath, std::vector<ArchiveFileEntry>& outFiles) {
    std::ifstream in(archivePath, std::ios::binary);
    if (!in.is_open()) return false;

    char magic[4];
    in.read(magic, sizeof(magic));
    if (!in.good() || std::memcmp(magic, kMagic, sizeof(kMagic)) != 0) return false;

    uint32_t version = 0;
    if (!readRaw(in, version) || version != kVersion) return false;

    uint32_t fileCount = 0;
    if (!readRaw(in, fileCount)) return false;

    std::vector<ArchiveFileEntry> files;
    files.reserve(fileCount);
    for (uint32_t i = 0; i < fileCount; ++i) {
        uint32_t nameLength = 0;
        if (!readRaw(in, nameLength)) return false;
        std::string name(nameLength, '\0');
        in.read(name.data(), nameLength);
        if (!in.good()) return false;

        uint64_t uncompressedSize = 0;
        uint64_t compressedSize = 0;
        if (!readRaw(in, uncompressedSize) || !readRaw(in, compressedSize)) return false;

        std::vector<uint8_t> compressed(compressedSize);
        if (compressedSize > 0) {
            in.read(reinterpret_cast<char*>(compressed.data()), static_cast<std::streamsize>(compressedSize));
            if (!in.good()) return false;
        }

        std::vector<uint8_t> data(uncompressedSize);
        if (uncompressedSize > 0) {
            uLongf destLen = static_cast<uLongf>(uncompressedSize);
            int result = uncompress(data.data(), &destLen, compressed.data(), static_cast<uLong>(compressedSize));
            if (result != Z_OK || destLen != uncompressedSize) {
                std::fprintf(stderr, "PackageArchive: real inflate failed for \"%s\" (zlib error %d)\n", name.c_str(),
                             result);
                return false;
            }
        }

        files.push_back(ArchiveFileEntry{std::move(name), std::move(data)});
    }

    outFiles = std::move(files);
    return true;
}

std::string assetManifestFileName() { return "assets_manifest.txt"; }
std::string thumbnailFileName() { return "thumbnail.ppm"; }

bool writeWorldPackageArchive(const WorldPackage& package, const std::string& assetRootDirectory,
                               const std::vector<std::string>& referencedAssetPaths, const std::string& archivePath,
                               const std::string& thumbnailSourcePath) {
    std::string tempDir = archivePath + ".tmp_build";
    std::error_code ec;
    std::filesystem::remove_all(tempDir, ec); // real, honest cleanup of any stale leftover from a prior failed attempt

    if (!package.saveToDirectory(tempDir)) return false;

    // Real asset bytes, content-addressed within the archive so two
    // different relative paths that happen to be byte-identical are
    // only ever stored once. `assetToArchivedName` is the real dedup
    // table this loop builds as it goes.
    std::unordered_map<std::string, std::string> hashToArchivedName; // sha256 hex -> archived flat filename
    std::ostringstream manifest;
    for (const std::string& relativePath : referencedAssetPaths) {
        std::filesystem::path resolved = assetRootDirectory.empty()
                                              ? std::filesystem::path(relativePath)
                                              : std::filesystem::path(assetRootDirectory) / relativePath;
        bool readOk = false;
        std::string bytes = readWholeBinaryFile(resolved.string(), readOk);
        if (!readOk) {
            // A real, honest gap: this reference was never found on
            // disk at export time. Recorded, not silently dropped and
            // not a hard failure of the whole export -- a creator's
            // package with one stale reference should still export
            // everything else real and usable.
            manifest << relativePath << "\t" << "\n";
            continue;
        }

        std::string hashHex = hexEncode(core::sha256(bytes));
        auto existing = hashToArchivedName.find(hashHex);
        std::string archivedName;
        if (existing != hashToArchivedName.end()) {
            archivedName = existing->second; // real dedup: identical content already queued once
        } else {
            archivedName = "asset_" + hashHex + extensionOf(relativePath);
            hashToArchivedName.emplace(hashHex, archivedName);
            std::ofstream assetOut(tempDir + "/" + archivedName, std::ios::binary | std::ios::trunc);
            assetOut.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        }
        manifest << relativePath << "\t" << archivedName << "\n";
    }
    {
        std::ofstream manifestOut(tempDir + "/" + assetManifestFileName(), std::ios::trunc);
        manifestOut << manifest.str();
    }

    if (!thumbnailSourcePath.empty() && std::filesystem::exists(thumbnailSourcePath)) {
        std::filesystem::copy_file(thumbnailSourcePath, tempDir + "/" + thumbnailFileName(),
                                    std::filesystem::copy_options::overwrite_existing, ec);
    }

    std::vector<ArchiveFileEntry> files;
    for (const auto& dirEntry : std::filesystem::directory_iterator(tempDir)) {
        if (!dirEntry.is_regular_file()) continue;
        std::ifstream in(dirEntry.path(), std::ios::binary);
        if (!in.is_open()) {
            std::filesystem::remove_all(tempDir, ec);
            return false;
        }
        std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        files.push_back(ArchiveFileEntry{dirEntry.path().filename().string(), std::move(data)});
    }

    bool ok = writeArchive(archivePath, files);
    std::filesystem::remove_all(tempDir, ec);
    return ok;
}

bool extractWorldPackageArchive(const std::string& archivePath, const std::string& outputDirectory) {
    std::vector<ArchiveFileEntry> files;
    if (!readArchive(archivePath, files)) return false;

    std::error_code ec;
    std::filesystem::create_directories(outputDirectory, ec);

    // Every flat file inside the archive (scene.txt/metadata.json/
    // package.json/thumbnail/each content-addressed asset_<hash> file)
    // written back at the SAME flat name first -- restoring an asset to
    // its real original relative path (which may include real
    // subdirectories the flat archive storage doesn't) is a second,
    // separate pass below, once the manifest itself has been read back.
    std::unordered_map<std::string, const ArchiveFileEntry*> byName;
    for (const ArchiveFileEntry& file : files) {
        byName.emplace(file.relativePath, &file);
        std::ofstream out(outputDirectory + "/" + file.relativePath, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) return false;
        if (!file.data.empty()) out.write(reinterpret_cast<const char*>(file.data.data()),
                                           static_cast<std::streamsize>(file.data.size()));
    }

    auto manifestIt = byName.find(assetManifestFileName());
    if (manifestIt == byName.end()) return true; // real, honest: a package with no referenced assets has no manifest to walk

    std::string manifestText(reinterpret_cast<const char*>(manifestIt->second->data.data()),
                              manifestIt->second->data.size());
    std::istringstream manifestIn(manifestText);
    std::string line;
    while (std::getline(manifestIn, line)) {
        if (line.empty()) continue;
        size_t tab = line.find('\t');
        if (tab == std::string::npos) continue;
        std::string originalRelativePath = line.substr(0, tab);
        std::string archivedName = line.substr(tab + 1);
        if (archivedName.empty()) continue; // this reference was never found at export time -- see its own comment above

        auto assetIt = byName.find(archivedName);
        if (assetIt == byName.end()) continue; // real, honest: the manifest names a file the archive doesn't actually contain

        std::filesystem::path destination = std::filesystem::path(outputDirectory) / originalRelativePath;
        std::filesystem::create_directories(destination.parent_path(), ec);
        std::ofstream out(destination, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) return false;
        const std::vector<uint8_t>& data = assetIt->second->data;
        if (!data.empty()) out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    }

    return true;
}

std::string archiveSha256Hex(const std::string& archivePath) {
    bool ok = false;
    std::string bytes = readWholeBinaryFile(archivePath, ok);
    if (!ok) return {};
    return hexEncode(core::sha256(bytes));
}

} // namespace engine::publishing
