#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "safety/IPInfringementScanner.hpp"

namespace engine::safety {

struct AssetSafetyFinding {
    std::string description;
    bool blocking = false; // true => caller MUST reject the asset, not just log/flag it
};

struct AssetSafetyReport {
    std::vector<AssetSafetyFinding> findings; // empty => nothing suspicious found

    [[nodiscard]] bool anyFlagged() const { return !findings.empty(); }
    [[nodiscard]] bool anyBlocked() const;
};

// Scans an uploaded image asset's *file bytes* -- not the decoded pixels
// core::Texture::loadFromFile() hands to the GPU, the raw bytes as
// uploaded -- for three real, independently well-known upload-pipeline
// risks, before anything reaches stb_image (the same vendored decoder
// core::Texture uses):
//
//   1. Magic-byte/extension mismatch: the classic file-type-confusion
//      trick -- a file claiming to be "icon.png" whose actual header
//      isn't a PNG signature at all. Detected via checkFormatMismatch()
//      reading the first few bytes; does not attempt to identify what
//      the file *actually* is beyond "not what it claims to be", since
//      that's already enough to reject it.
//   2. Decompression-bomb dimensions: PNG/JPEG headers carry width/height
//      before any pixel data is decoded, so a hostile file can claim a
//      trivial file size but an enormous decoded footprint (e.g. a
//      100x100 PNG with a corrupted-on-purpose... more precisely, a
//      genuinely tiny *compressed* file whose IHDR/SOF0 claims a
//      40000x40000 image -- ~4.8GB of RGBA once decoded). Rejected by
//      reading just the dimension fields, never by decoding pixels.
//   3. Smuggled metadata text: PNG tEXt/iTXt chunks and JPEG COM segments
//      can carry arbitrary text invisible in the rendered image itself --
//      a real vector for embedding IP-infringing names/slurs/URLs that a
//      human reviewer glancing at the *image* would never see. Extracted
//      text is run through the same IPInfringementScanner content titles
//      are scanned with.
//
// PNG and JPEG only -- the two formats core::Texture actually loads via
// stb_image; there is no reason for this guard to understand formats
// this engine doesn't decode. This is real chunk/marker-format parsing
// (PNG's length-prefixed chunk stream, JPEG's marker-segment stream),
// not a stub, but it is deliberately narrow: it reads structural headers
// and plaintext metadata fields only, never decodes pixel/entropy-coded
// data (no zTXt/compressed-iTXt inflate, no JPEG scan-data parsing past
// the first SOF marker) -- see the .cpp for exactly where each parser
// stops and why. All parsing is bounds-checked against untrusted,
// possibly truncated or adversarially malformed input by construction --
// this runs on arbitrary creator uploads, so it must never read past the
// buffer it's given, only ever produce a best-effort partial result.
class AssetSafetyGuard {
public:
    [[nodiscard]] AssetSafetyReport scanImageAsset(const std::vector<uint8_t>& fileBytes,
                                                     const std::string& claimedExtension,
                                                     const IPInfringementScanner& textScanner) const;
};

} // namespace engine::safety
