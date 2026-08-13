#include "safety/AssetSafetyGuard.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace engine::safety {

namespace {

// Illustrative cap, not a claim about what a real deployment's asset
// pipeline should allow -- the point is bounding worst-case decoded
// memory (kMaxImageDimension^2 * 4 bytes for RGBA) to something sane
// before ever decoding a single pixel, not picking the "correct" limit.
constexpr uint32_t kMaxImageDimension = 8192;

enum class DetectedFormat { Png, Jpeg, Unknown };

DetectedFormat detectFormat(const std::vector<uint8_t>& bytes) {
    static constexpr uint8_t kPngSignature[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    if (bytes.size() >= 8 && std::memcmp(bytes.data(), kPngSignature, 8) == 0) return DetectedFormat::Png;
    if (bytes.size() >= 3 && bytes[0] == 0xFF && bytes[1] == 0xD8 && bytes[2] == 0xFF) return DetectedFormat::Jpeg;
    return DetectedFormat::Unknown;
}

std::string normalizedExtension(const std::string& claimed) {
    std::string ext = claimed;
    if (!ext.empty() && ext.front() == '.') ext.erase(ext.begin());
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

bool extensionMatchesFormat(const std::string& claimed, DetectedFormat format) {
    std::string ext = normalizedExtension(claimed);
    switch (format) {
        case DetectedFormat::Png: return ext == "png";
        case DetectedFormat::Jpeg: return ext == "jpg" || ext == "jpeg";
        case DetectedFormat::Unknown: return false;
    }
    return false;
}

void addFinding(AssetSafetyReport& report, std::string description, bool blocking) {
    report.findings.push_back(AssetSafetyFinding{std::move(description), blocking});
}

void checkDimensions(AssetSafetyReport& report, uint32_t width, uint32_t height) {
    if (width == 0 || height == 0 || width > kMaxImageDimension || height > kMaxImageDimension) {
        addFinding(report,
                    "Declared image dimensions (" + std::to_string(width) + "x" + std::to_string(height) +
                        ") are zero or exceed the " + std::to_string(kMaxImageDimension) +
                        "px sanity limit -- possible decompression bomb",
                    /*blocking=*/true);
    }
}

void scanExtractedText(AssetSafetyReport& report, const std::string& text, const IPInfringementScanner& textScanner,
                        const char* sourceLabel) {
    if (text.empty()) return;
    IPInfringementResult scanResult = textScanner.scan(text);
    if (!scanResult.flagged) return;
    addFinding(report,
                std::string("Embedded ") + sourceLabel + " metadata matched IP-infringement scan: \"" + text + "\"",
                scanResult.blocked);
}

uint32_t readBigEndianU32(const std::vector<uint8_t>& bytes, size_t offset) {
    return (static_cast<uint32_t>(bytes[offset]) << 24) | (static_cast<uint32_t>(bytes[offset + 1]) << 16) |
           (static_cast<uint32_t>(bytes[offset + 2]) << 8) | static_cast<uint32_t>(bytes[offset + 3]);
}

uint16_t readBigEndianU16(const std::vector<uint8_t>& bytes, size_t offset) {
    return static_cast<uint16_t>((static_cast<uint16_t>(bytes[offset]) << 8) | static_cast<uint16_t>(bytes[offset + 1]));
}

// Walks PNG's chunk stream (8-byte signature already verified by the
// caller) -- each chunk is [4-byte big-endian length][4-byte type][length
// bytes of data][4-byte CRC, unchecked -- verifying it isn't a safety
// property, just a corruption check stb_image already performs at real
// decode time]. Every offset arithmetic step is bounds-checked against
// bytes.size() before use -- this parses untrusted uploads, so a
// truncated or adversarially-malformed length field must stop parsing
// cleanly, never read out of bounds.
void scanPng(const std::vector<uint8_t>& bytes, const IPInfringementScanner& textScanner, AssetSafetyReport& report) {
    size_t offset = 8; // past the 8-byte PNG signature
    bool sawIhdr = false;

    while (offset + 8 <= bytes.size()) {
        uint32_t chunkLength = readBigEndianU32(bytes, offset);
        std::string chunkType(reinterpret_cast<const char*>(bytes.data() + offset + 4), 4);
        size_t dataStart = offset + 8;

        // dataStart + chunkLength + 4 (CRC) must fit -- checked as a
        // single unsigned comparison guarded against chunkLength itself
        // being large enough to overflow the addition.
        if (chunkLength > bytes.size() || dataStart > bytes.size() - chunkLength ||
            dataStart + chunkLength > bytes.size() - 4) {
            addFinding(report, "PNG chunk '" + chunkType + "' declares a length past the end of the file",
                        /*blocking=*/true);
            break;
        }

        if (chunkType == "IHDR" && chunkLength >= 8) {
            checkDimensions(report, readBigEndianU32(bytes, dataStart), readBigEndianU32(bytes, dataStart + 4));
            sawIhdr = true;
        } else if (chunkType == "tEXt") {
            const uint8_t* data = bytes.data() + dataStart;
            const uint8_t* nul = static_cast<const uint8_t*>(std::memchr(data, '\0', chunkLength));
            if (nul != nullptr) {
                size_t keywordLen = static_cast<size_t>(nul - data);
                std::string text(reinterpret_cast<const char*>(nul + 1), chunkLength - keywordLen - 1);
                scanExtractedText(report, text, textScanner, "PNG tEXt");
            }
        } else if (chunkType == "iTXt") {
            // keyword\0 compressionFlag(1) compressionMethod(1) languageTag\0 translatedKeyword\0 text
            const uint8_t* data = bytes.data() + dataStart;
            const uint8_t* keywordEnd = static_cast<const uint8_t*>(std::memchr(data, '\0', chunkLength));
            if (keywordEnd != nullptr) {
                size_t afterKeyword = static_cast<size_t>(keywordEnd - data) + 1;
                if (afterKeyword + 2 <= chunkLength && data[afterKeyword] == 0) { // compressionFlag == 0 => plaintext
                    size_t cursor = afterKeyword + 2; // past compressionFlag + compressionMethod
                    const uint8_t* langEnd =
                        static_cast<const uint8_t*>(std::memchr(data + cursor, '\0', chunkLength - cursor));
                    if (langEnd != nullptr) {
                        size_t afterLang = static_cast<size_t>(langEnd - data) + 1;
                        const uint8_t* translatedEnd =
                            static_cast<const uint8_t*>(std::memchr(data + afterLang, '\0', chunkLength - afterLang));
                        if (translatedEnd != nullptr) {
                            size_t textStart = static_cast<size_t>(translatedEnd - data) + 1;
                            std::string text(reinterpret_cast<const char*>(data + textStart), chunkLength - textStart);
                            scanExtractedText(report, text, textScanner, "PNG iTXt");
                        }
                    }
                }
                // compressionFlag == 1 (zlib-compressed iTXt text) is left
                // unscanned -- see class doc comment on why this parser
                // doesn't inflate compressed metadata.
            }
        }

        offset = dataStart + chunkLength + 4; // past data + CRC
        if (chunkType == "IEND") break;
    }

    if (!sawIhdr) {
        addFinding(report, "PNG signature present but no valid IHDR chunk found -- malformed or truncated file",
                    /*blocking=*/true);
    }
}

// Walks JPEG's marker-segment stream starting after the SOI marker
// (already verified by the caller). Stops at the first SOS (start of
// scan) marker -- entropy-coded scan data follows SOS and isn't
// marker-segment-structured the same way, so this parser deliberately
// doesn't attempt to skip over it (every dimension/COM marker in a
// standard JPEG file precedes the first SOS, so nothing of interest is
// missed by stopping there).
void scanJpeg(const std::vector<uint8_t>& bytes, const IPInfringementScanner& textScanner, AssetSafetyReport& report) {
    size_t offset = 2; // past the 0xFF 0xD8 SOI marker
    bool sawDimensions = false;

    while (offset + 1 < bytes.size()) {
        if (bytes[offset] != 0xFF) {
            addFinding(report, "JPEG marker stream desynchronized (expected 0xFF) -- malformed file", /*blocking=*/true);
            break;
        }
        uint8_t marker = bytes[offset + 1];
        offset += 2;
        if (marker == 0xFF) continue; // padding byte before the real marker, re-read
        if (marker == 0xD9) break;    // EOI
        if (marker == 0xDA) break;    // SOS -- entropy data follows, stop (see comment above)
        if ((marker >= 0xD0 && marker <= 0xD7) || marker == 0x01) continue; // RST/TEM: no length field

        if (offset + 2 > bytes.size()) break; // truncated before a length field
        uint16_t segmentLength = readBigEndianU16(bytes, offset); // includes the 2 length bytes themselves
        if (segmentLength < 2 || offset + segmentLength > bytes.size()) {
            addFinding(report, "JPEG marker segment declares a length past the end of the file", /*blocking=*/true);
            break;
        }
        size_t payloadStart = offset + 2;
        size_t payloadLength = static_cast<size_t>(segmentLength) - 2;

        if ((marker == 0xC0 || marker == 0xC1 || marker == 0xC2 || marker == 0xC3) && payloadLength >= 5) {
            // SOF0/1/2/3: precision(1) height(2) width(2) ...
            checkDimensions(report, readBigEndianU16(bytes, payloadStart + 3), readBigEndianU16(bytes, payloadStart + 1));
            sawDimensions = true;
        } else if (marker == 0xFE) { // COM
            std::string text(reinterpret_cast<const char*>(bytes.data() + payloadStart), payloadLength);
            scanExtractedText(report, text, textScanner, "JPEG COM");
        }

        offset += segmentLength;
    }

    if (!sawDimensions) {
        addFinding(report, "JPEG signature present but no SOF dimension marker found -- malformed or truncated file",
                    /*blocking=*/true);
    }
}

} // namespace

bool AssetSafetyReport::anyBlocked() const {
    return std::any_of(findings.begin(), findings.end(), [](const AssetSafetyFinding& f) { return f.blocking; });
}

AssetSafetyReport AssetSafetyGuard::scanImageAsset(const std::vector<uint8_t>& fileBytes,
                                                     const std::string& claimedExtension,
                                                     const IPInfringementScanner& textScanner) const {
    AssetSafetyReport report;

    DetectedFormat format = detectFormat(fileBytes);
    if (format == DetectedFormat::Unknown) {
        addFinding(report, "File signature does not match any supported image format (PNG/JPEG)", /*blocking=*/true);
        return report;
    }
    if (!extensionMatchesFormat(claimedExtension, format)) {
        addFinding(report,
                    "File extension '." + normalizedExtension(claimedExtension) +
                        "' does not match the file's actual signature (" +
                        (format == DetectedFormat::Png ? "PNG" : "JPEG") + ")",
                    /*blocking=*/true);
        // Still scan the real content below -- the mismatch alone already
        // blocks, but surfacing every finding (not just the first) gives
        // a human reviewer the full picture in one pass.
    }

    if (format == DetectedFormat::Png) {
        scanPng(fileBytes, textScanner, report);
    } else {
        scanJpeg(fileBytes, textScanner, report);
    }

    return report;
}

} // namespace engine::safety
