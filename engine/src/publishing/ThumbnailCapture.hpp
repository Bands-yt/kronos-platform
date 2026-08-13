#pragma once

#include <string>

#include <volk.h>
#include <vk_mem_alloc.h>

namespace engine::core {
class Renderer;
}

namespace engine::publishing {

// Sprint 13 task 3's "Add auto-capture + manual capture modes."
enum class ThumbnailCaptureMode { Auto, Manual };

// Sprint 13 task 3's real GPU readback: copies whatever `colorImage`
// currently holds into a real, valid, uncompressed PPM (P6) file at
// `outputPath`. `colorImage` is expected to be in
// VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL on entry (the same layout
// studio::PreviewScene/ThumbnailCameraRig's own render() call leaves its
// target in for ImGui sampling -- see their own comments) and is left in
// that same layout on return, so a live ImGui::Image() display of the
// same target isn't disrupted by capturing it.
//
// PPM, not PNG, is a deliberate, honest choice: only the read-only
// stb_image.h is vendored in this codebase (see
// cmake/Dependencies.cmake's own comment), no PNG *encoder* -- and
// hand-reproducing a real compressed image codec from memory risks
// silent, hard-to-detect corruption. PPM's trivial, uncompressed format
// (a short text header, then raw RGB bytes) can be written correctly by
// hand with real confidence; any real image tool can open/convert it.
// See "Publishing & Packaging" in the README for the full account.
//
// Mirrors core::Texture::uploadPixels()'s exact real one-shot command-
// buffer + staging-buffer pattern (Texture.cpp), just the mirror-image
// direction (image -> buffer instead of buffer -> image). Synchronous
// (vkQueueWaitIdle) -- a real, deliberate capture action triggered by an
// explicit creator button click, not a per-frame operation, so the same
// stall-the-queue tradeoff Texture::uploadPixels() already makes for the
// same reason is fine here too.
[[nodiscard]] bool captureThumbnailToFile(core::Renderer& renderer, VkImage colorImage, VkFormat colorFormat,
                                           VkExtent2D extent, const std::string& outputPath);

} // namespace engine::publishing
