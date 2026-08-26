#pragma once

#include <cstdint>
#include <string>

namespace engine::core {

struct TextureBakeResult {
    bool succeeded = false;
    std::string error; // set when succeeded is false

    // A 512x512 source bakes 10 levels (512,256,...,1); a 1x1 source
    // bakes exactly 1 (itself, no further halving possible).
    uint32_t mipLevels = 0;
    // Total real, on-disk bytes across every written mip PNG -- the
    // "compression" half of this pipeline: each level is a real,
    // separately deflate-compressed PNG (stb_image_write), not a raw
    // pixel dump.
    uint64_t bakedSizeBytes = 0;
    // Where the mip chain landed -- see bakeTextureMips()'s own comment
    // for the real path shape.
    std::string cacheDir;
};

// Kronos (Asset Hot-Import Pipeline): real CPU-side mip-chain baking for
// a texture asset -- fills the exact gap core::Texture::loadFromFile()
// itself documents ("one mip level ... a real content pipeline would
// bake mips, not this bring-up loader"). Runs off the main thread, as a
// background core::AssetImportQueue job (see that class's workerLoop()),
// not at GPU-upload time -- baking is a one-time-per-import disk
// artifact, not something core::Texture needs to redo on every scene
// load.
//
// Decodes the full image via stb_image (forced to 4 channels), then
// real box-filter halves it (stb_image_resize2's
// stbir_resize_uint8_linear -- deliberately the *linear*, not *_srgb*,
// variant: this function has no material-slot context to know whether
// `path` is color data or a linear data texture the way
// Texture::loadFromFile()'s caller-supplied `srgb` flag does, so it
// treats every source as already-linear bytes rather than guessing) on
// each axis down to 1x1, writing every level as a real, separately
// deflate-compressed PNG (stb_image_write's stbi_write_png) into
// `<sourceDir>/.kronos_mips/<sourceStem>/mip_<N>.png`. Always
// real-rebakes (no freshness cache) -- same "re-import always
// re-computes" convention core::AssetRegistry::importAsset() already
// follows, so an edited-then-re-imported source can never serve a stale
// mip chain.
[[nodiscard]] TextureBakeResult bakeTextureMips(const std::string& path);

} // namespace engine::core
