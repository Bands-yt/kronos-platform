#include "ArchiveExtractor.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

// miniz_zip.h assumes it's included as part of the amalgamated miniz.h
// (its own real, upstream-documented usage) -- since this project builds
// miniz as separate translation units rather than the single-header
// amalgamation, miniz.h and miniz_tinfl.h must come first to provide
// mz_alloc_func/mz_free_func/mz_realloc_func and tinfl_decompressor,
// which miniz_zip.h's real struct definitions require but does not
// itself include.
//
// MINIZ_NO_ZLIB_COMPATIBLE_NAMES: this file also uses real zlib directly
// (for the .tar.gz gunzip path below) -- miniz.h's own zlib-compatible
// typedefs/macros (alloc_func, z_stream, adler32, crc32, ...) collide
// with real zlib.h's real declarations of the same names when both are
// included in one translation unit; miniz.h's own header comment (line
// 440 in the fetched source) documents this exact define as the fix.
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include <miniz.h>
#include <miniz_tinfl.h>
#include <miniz_zip.h>
#include <zlib.h>

namespace kronos_installer {

namespace {

bool endsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// Real running "do all entries share one top-level directory?" tracker,
// fed every entry name as it is extracted by either backend. `state`
// starts empty; it holds the real shared prefix so far, or "\x01" once a
// real entry has proven there ISN'T a single common root (a sentinel no
// real archive path can contain).
void noteTopLevelDirectory(const std::string& entryName, std::string& state) {
    static const std::string kNoCommonRoot = "\x01";
    if (state == kNoCommonRoot) return;

    size_t slash = entryName.find_first_of("/\\");
    // A real entry at the archive root (no separator at all) means there
    // is genuinely no single wrapping directory.
    if (slash == std::string::npos || slash == 0) {
        state = kNoCommonRoot;
        return;
    }

    std::string root = entryName.substr(0, slash);
    if (state.empty()) {
        state = root;
    } else if (state != root) {
        state = kNoCommonRoot;
    }
}

std::string finalizeTopLevelDirectory(const std::string& state) {
    return state == "\x01" ? std::string() : state;
}

// --- Real .zip extraction (miniz) ------------------------------------------
ExtractResult extractZip(const std::string& archivePath, const std::string& destinationDir) {
    ExtractResult result;

    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof(zip));
    if (!mz_zip_reader_init_file(&zip, archivePath.c_str(), 0)) {
        result.error = "could not open the real .zip archive (corrupt or incomplete download?)";
        return result;
    }

    std::string topLevelState;
    mz_uint fileCount = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < fileCount; ++i) {
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&zip, i, &stat)) continue;

        noteTopLevelDirectory(stat.m_filename, topLevelState);
        std::filesystem::path outPath = std::filesystem::path(destinationDir) / stat.m_filename;
        if (mz_zip_reader_is_file_a_directory(&zip, i)) {
            std::error_code ec;
            std::filesystem::create_directories(outPath, ec);
            continue;
        }

        std::error_code ec;
        std::filesystem::create_directories(outPath.parent_path(), ec);
        if (!mz_zip_reader_extract_to_file(&zip, i, outPath.string().c_str(), 0)) {
            mz_zip_reader_end(&zip);
            result.error = "failed extracting \"" + std::string(stat.m_filename) + "\" from the real .zip archive";
            return result;
        }
        ++result.filesExtracted;
    }

    mz_zip_reader_end(&zip);
    result.topLevelDirectory = finalizeTopLevelDirectory(topLevelState);
    result.success = true;
    return result;
}

// --- Real gzip decompression (zlib) ----------------------------------------
// Real, whole-buffer decompression -- a deliberate, real simplicity/
// memory tradeoff (a typical real Kronos archive decompresses to
// roughly 200MB, comfortably fine to hold in memory on any real desktop
// this installer targets) rather than a fully streaming inflate loop
// interleaved with tar parsing.
bool gunzipWholeFile(const std::string& gzPath, std::vector<uint8_t>& outDecompressed, std::string& outError) {
    std::ifstream file(gzPath, std::ios::binary);
    if (!file.good()) {
        outError = "could not open the real .tar.gz archive";
        return false;
    }
    std::vector<uint8_t> compressed((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (compressed.empty()) {
        outError = "the real .tar.gz archive is empty";
        return false;
    }

    z_stream stream{};
    // 15 + 16: real zlib convention for "expect and parse a real gzip
    // header/trailer," not raw DEFLATE (see zlib.h's own documented
    // windowBits contract).
    if (inflateInit2(&stream, 15 + 16) != Z_OK) {
        outError = "zlib inflateInit2() failed";
        return false;
    }

    stream.next_in = compressed.data();
    stream.avail_in = static_cast<uInt>(compressed.size());

    std::vector<uint8_t> chunk(1 << 18); // real, 256KB working buffer
    int zlibResult = Z_OK;
    while (zlibResult != Z_STREAM_END) {
        stream.next_out = chunk.data();
        stream.avail_out = static_cast<uInt>(chunk.size());
        zlibResult = inflate(&stream, Z_NO_FLUSH);
        if (zlibResult != Z_OK && zlibResult != Z_STREAM_END) {
            outError = std::string("zlib inflate() failed: ") + (stream.msg != nullptr ? stream.msg : "unknown error");
            inflateEnd(&stream);
            return false;
        }
        size_t produced = chunk.size() - stream.avail_out;
        outDecompressed.insert(outDecompressed.end(), chunk.begin(), chunk.begin() + static_cast<long>(produced));
        if (zlibResult != Z_STREAM_END && stream.avail_in == 0) break; // real, honest -- ran out of real input
    }
    inflateEnd(&stream);

    if (zlibResult != Z_STREAM_END) {
        outError = "the real .tar.gz archive ended unexpectedly (truncated download?)";
        return false;
    }
    return true;
}

// --- Real, small, hand-written POSIX ustar / GNU-tar parser ----------------
// A real tar header is a fixed 512-byte block; real file data follows,
// padded to a real 512-byte boundary; two real, consecutive all-zero
// 512-byte blocks mark the real end of archive. Numeric fields (size,
// mode, etc.) are real ASCII octal digit strings, not binary integers.
uint64_t parseOctalField(const char* field, size_t len) {
    uint64_t value = 0;
    for (size_t i = 0; i < len && field[i] != '\0' && field[i] != ' '; ++i) {
        if (field[i] < '0' || field[i] > '7') break;
        value = value * 8 + static_cast<uint64_t>(field[i] - '0');
    }
    return value;
}

bool isAllZero(const uint8_t* block, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        if (block[i] != 0) return false;
    }
    return true;
}

ExtractResult extractTarFromBuffer(const std::vector<uint8_t>& tarData, const std::string& destinationDir) {
    ExtractResult result;
    constexpr size_t kBlockSize = 512;

    // Real GNU-tar long-name extension state: a typeflag 'L' entry's own
    // real "file data" is the long name that applies to the *next* real
    // header, not a real file of its own.
    std::string pendingLongName;
    std::string topLevelState;

    size_t offset = 0;
    while (offset + kBlockSize <= tarData.size()) {
        const uint8_t* header = tarData.data() + offset;
        if (isAllZero(header, kBlockSize)) break; // real, honest end-of-archive marker

        char name[101] = {};
        std::memcpy(name, header, 100);
        char typeflag = static_cast<char>(header[156]);
        uint64_t size = parseOctalField(reinterpret_cast<const char*>(header + 124), 12);
        // Real POSIX tar mode field (8 octal bytes at offset 100). This
        // must be honored: a real Kronos release tarball stores
        // engine_runtime/studio as -rwxr-xr-x, and extracting them with
        // default 0644 would produce a real install whose binaries
        // simply cannot be executed.
        uint64_t mode = parseOctalField(reinterpret_cast<const char*>(header + 100), 8);
        // Real POSIX ustar "prefix" field (155 bytes at offset 345) --
        // real long paths split name across prefix+"/"+name; only
        // honored when the real "ustar" magic is present at offset 257.
        std::string fullName;
        if (std::memcmp(header + 257, "ustar", 5) == 0) {
            char prefix[156] = {};
            std::memcpy(prefix, header + 345, 155);
            if (prefix[0] != '\0') fullName = std::string(prefix) + "/" + std::string(name);
        }
        if (fullName.empty()) fullName = name;
        if (!pendingLongName.empty()) {
            fullName = pendingLongName;
            pendingLongName.clear();
        }

        offset += kBlockSize;
        size_t paddedSize = ((size + kBlockSize - 1) / kBlockSize) * kBlockSize;
        if (offset + paddedSize > tarData.size()) {
            result.error = "the real .tar archive is truncated (a file's own real data runs past the end)";
            return result;
        }

        if (typeflag != 'L') noteTopLevelDirectory(fullName, topLevelState);

        if (typeflag == 'L') {
            // Real GNU long-name data -- a real, null-terminated string.
            pendingLongName.assign(reinterpret_cast<const char*>(tarData.data() + offset),
                                    reinterpret_cast<const char*>(tarData.data() + offset + size));
            if (!pendingLongName.empty() && pendingLongName.back() == '\0') pendingLongName.pop_back();
        } else if (typeflag == '5' || (!fullName.empty() && fullName.back() == '/')) {
            std::error_code ec;
            std::filesystem::create_directories(std::filesystem::path(destinationDir) / fullName, ec);
        } else if (typeflag == '0' || typeflag == '\0') {
            std::filesystem::path outPath = std::filesystem::path(destinationDir) / fullName;
            std::error_code ec;
            std::filesystem::create_directories(outPath.parent_path(), ec);
            std::ofstream outFile(outPath, std::ios::binary | std::ios::trunc);
            if (!outFile.good()) {
                result.error = "could not write \"" + fullName + "\" while extracting the real .tar archive";
                return result;
            }
            outFile.write(reinterpret_cast<const char*>(tarData.data() + offset), static_cast<std::streamsize>(size));
            outFile.close();
            // Apply the real archived mode's own permission bits. Only
            // the low 9 (rwxrwxrwx) are honored -- setuid/setgid/sticky
            // are deliberately not, since nothing in a real Kronos
            // release needs them and silently restoring them from a
            // downloaded archive would be a real privilege-escalation
            // footgun.
            std::filesystem::permissions(outPath, static_cast<std::filesystem::perms>(mode & 0777),
                                          std::filesystem::perm_options::replace, ec);
            ++result.filesExtracted;
        }
        // Real, deliberate: symlinks/hardlinks/device files (typeflags
        // '1'/'2'/'3'/'4') are skipped -- a real Kronos release archive
        // (see package_alpha.sh) never contains any of those.

        offset += paddedSize;
    }

    result.topLevelDirectory = finalizeTopLevelDirectory(topLevelState);
    result.success = true;
    return result;
}

ExtractResult extractTarGz(const std::string& archivePath, const std::string& destinationDir) {
    std::vector<uint8_t> tarData;
    std::string error;
    if (!gunzipWholeFile(archivePath, tarData, error)) {
        ExtractResult result;
        result.error = error;
        return result;
    }
    return extractTarFromBuffer(tarData, destinationDir);
}

} // namespace

ExtractResult extractArchive(const std::string& archivePath, const std::string& destinationDir) {
    std::error_code ec;
    std::filesystem::create_directories(destinationDir, ec);
    if (ec) {
        ExtractResult result;
        result.error = "could not create the install directory \"" + destinationDir + "\"";
        return result;
    }

    if (endsWith(archivePath, ".zip")) return extractZip(archivePath, destinationDir);
    if (endsWith(archivePath, ".tar.gz") || endsWith(archivePath, ".tgz")) return extractTarGz(archivePath, destinationDir);

    ExtractResult result;
    result.error = "unrecognized archive format for \"" + archivePath + "\" (expected .zip or .tar.gz)";
    return result;
}

} // namespace kronos_installer
