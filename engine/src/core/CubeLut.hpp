#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace engine::core {

// Kronos ("Cinematic Camera Physics & Post-Processing Pipeline" -- real
// 3D LUT color grading): pure data + parsing, zero Vulkan -- the GPU
// upload half (Renderer::loadColorGradingLut()) is real, separate,
// GPU-touching scope, same "pure logic apart from the GPU-touching code
// that consumes it" split this codebase uses throughout (e.g.
// cinematic::OfflineExport.hpp vs trailer::CaptureRig).
//
// `.cube` is the real, standard ASCII LUT interchange format most color
// grading tools (DaVinci Resolve, Adobe products, Blender) export --
// this parses the common real-world subset: TITLE (ignored), LUT_3D_SIZE
// N (required), '#' comments, blank lines, and exactly N^3 real "r g b"
// data lines in the format's own documented order (red fastest, then
// green, then blue). DOMAIN_MIN/DOMAIN_MAX are parsed and validated
// (both finite, min < max on every axis) so a file declaring them
// doesn't fail to parse, but are NOT applied to rescale sampling -- a
// real, stated limitation: shaders/composite.frag always samples with a
// fixed [0,1] input (the already-tonemapped color), so a LUT authored
// against a non-default domain (common for log/RAW-footage LUTs, rare
// for a final display "look" LUT applied post-tonemap, which is this
// pass's only real use case) won't reflect its declared remap. 1D LUTs
// (LUT_1D_SIZE) are real, separate, unsupported scope -- this engine's
// composite pass only has a 3D LUT binding.
struct CubeLutData {
    uint32_t size = 0;
    // size*size*size*3 floats, RGB triples in the same red-fastest order
    // the .cube format itself specifies -- this is also exactly the
    // memory layout Renderer::loadColorGradingLut() uploads verbatim
    // into a VK_FORMAT_R32G32B32A32_SFLOAT (converted to RGBA, alpha
    // real-set to 1.0) VK_IMAGE_TYPE_3D image.
    std::vector<float> rgb;

    [[nodiscard]] bool isValid() const { return size > 0 && rgb.size() == static_cast<size_t>(size) * size * size * 3; }
};

// Real, honest failure (outError set, returns false) for: a missing/
// unreadable file, no LUT_3D_SIZE line, a size outside [2, 256] (2 is
// the real Vulkan-legal minimum for a 3D image dimension; 256 is a
// generous real ceiling no real-world .cube file exceeds -- guards
// against a corrupt file claiming an absurd size and exhausting memory),
// or fewer/more data lines than size^3 actually requires. Never returns
// true with a partially-populated CubeLutData.
[[nodiscard]] bool parseCubeLutFile(const std::string& path, CubeLutData& outData, std::string& outError);

// A real, honest no-op LUT: identity(r,g,b) = (r,g,b) exactly, at every
// one of `size`'s grid points -- what Renderer seeds as the default
// before any real creator-authored .cube is ever loaded, so the
// composite pass's LUT sampling is always well-defined (a real texture,
// not a null/placeholder one) and visually inert (CompositePushConstants::
// lutStrength notwithstanding) until a creator explicitly loads one.
[[nodiscard]] CubeLutData generateIdentityCubeLut(uint32_t size);

} // namespace engine::core
