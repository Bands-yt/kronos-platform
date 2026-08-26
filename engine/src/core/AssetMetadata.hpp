#pragma once

#include <cstdint>
#include <string>

namespace engine::core {

enum class AssetKind { Mesh, Texture, Audio, Unknown };

[[nodiscard]] AssetKind detectAssetKind(const std::string& path);

struct AssetMetadata {
    bool succeeded = false;
    std::string error; // set when succeeded is false
    AssetKind kind = AssetKind::Unknown;
    uint64_t fileSizeBytes = 0;

    // Mesh (.obj)
    uint32_t vertexCount = 0;
    uint32_t triangleCount = 0;

    // Texture
    int32_t width = 0;
    int32_t height = 0;
    int32_t channels = 0;
    // Kronos (Asset Hot-Import Pipeline): filled in by
    // core::AssetImportQueue's worker (see its own workerLoop() comment)
    // after this metadata's own extractAssetMetadata() probe succeeds --
    // NOT set by extractAssetMetadata() itself, which only inspects the
    // header (stbi_info(), no pixel decode). 0 for every non-Texture
    // kind, and for a Texture whose mip bake itself failed (a real,
    // honest partial success: the import still registers, just without
    // baked mips -- see core::TextureBaker.hpp).
    uint32_t mipLevelsBaked = 0;
    uint64_t bakedMipSizeBytes = 0;

    // Audio
    double durationSeconds = 0.0;
    uint32_t sampleRate = 0;
    uint32_t channelCount = 0;
};

// Real inspection, not a filename guess: dispatches on detectAssetKind()
// (extension-based) to a *real* per-kind probe --
//   - Mesh (.obj): a full core::loadObj() parse, reporting real vertex/
//     triangle counts (this is the only kind that needs a full parse;
//     OBJ carries no header summarizing its own vertex count).
//   - Texture: stb_image's stbi_info() -- reads just the image header for
//     width/height/channel count, no pixel decode.
//   - Audio: a real miniaudio ma_decoder, opened just long enough to read
//     sample rate/channel count/frame count then closed -- no playback.
// Every kind also reports the real on-disk file size
// (std::filesystem::file_size), independent of which probe ran.
[[nodiscard]] AssetMetadata extractAssetMetadata(const std::string& path);

} // namespace engine::core
