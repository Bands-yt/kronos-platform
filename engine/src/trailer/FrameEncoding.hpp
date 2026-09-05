#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace engine::trailer {

// Real pixel-buffer -> file encoding, deliberately kept free of Vulkan
// types: every function here takes a raw pointer and a size, so a real
// synthetic in-memory buffer exercises the exact same encode path a real
// GPU readback feeds it, without needing a device. This is what makes
// the Offline Export pipeline's image-writing half real, headless-
// testable code rather than dead weight bundled inside CaptureRig's own
// untested (see that class's own header comment) Vulkan path.
//
// CaptureRig::exportSequence() is the only real caller: it does the
// actual GPU render + readback into memory, then hands the raw bytes
// here to reach disk.

// Writes `pixels` (width*height RGBA8, row-major, 4 bytes/texel, no row
// padding) as a PNG via stb_image_write (already vendored and used by
// core::TextureBaker -- see cmake/Dependencies.cmake's own comment).
// `swapRedBlue` matches ThumbnailCapture.cpp's own real convention: true
// when the source bytes are BGRA (the swapchain's own real in-memory
// format), so callers pass through the identical logic that function
// already established rather than re-deriving it.
[[nodiscard]] bool writePngRgba8(const uint8_t* pixels, uint32_t width, uint32_t height, bool swapRedBlue,
                                  const std::string& path);

// Writes `depth` (width*height, single-channel, row-major float) as a
// single-channel EXR via tinyexr (external/vendor/tinyexr -- see
// cmake/Dependencies.cmake's own comment on why it needs no extra
// dependency). Real float precision end to end: an 8-bit PNG would
// quantise away exactly the precision a compositor wants a depth pass
// for (see cinematic::ExportImageFormat's own comment).
[[nodiscard]] bool writeExrDepth(const float* depth, uint32_t width, uint32_t height, const std::string& path);

// Averages N same-sized RGBA8 sub-frame buffers into one, for motion
// blur accumulation (cinematic::MotionBlurSettings::subFrameSamples).
// Real "sum then round", not truncation, so an all-identical input
// round-trips byte-exact rather than drifting down by one per average.
// A sub-frame whose size disagrees with the first is skipped (a real
// caller bug -- every sample of one export job renders at the same
// resolution) rather than read past its end.
[[nodiscard]] std::vector<uint8_t> averageRgba8SubFrames(const std::vector<std::vector<uint8_t>>& subFrames);

// Same real averaging for float depth sub-frames -- no rounding needed,
// float division is exact enough for a depth pass.
[[nodiscard]] std::vector<float> averageDepthSubFrames(const std::vector<std::vector<float>>& subFrames);

} // namespace engine::trailer
