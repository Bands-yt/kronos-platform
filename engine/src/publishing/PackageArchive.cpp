#include "publishing/PackageArchive.hpp"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

#include <zlib.h>

namespace engine::publishing {

namespace {
constexpr char kMagic[4] = {'K', 'R', 'A', 'R'};
constexpr uint32_t kVersion = 1;

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

bool writeWorldPackageArchive(const WorldPackage& package, const std::vector<std::string>& referencedAssetPaths,
                               const std::string& archivePath, const std::string& thumbnailSourcePath) {
    std::string tempDir = archivePath + ".tmp_build";
    std::error_code ec;
    std::filesystem::remove_all(tempDir, ec); // real, honest cleanup of any stale leftover from a prior failed attempt

    if (!package.saveToDirectory(tempDir)) return false;

    {
        std::ofstream manifestOut(tempDir + "/" + assetManifestFileName(), std::ios::trunc);
        for (const std::string& assetPath : referencedAssetPaths) manifestOut << assetPath << "\n";
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

} // namespace engine::publishing
