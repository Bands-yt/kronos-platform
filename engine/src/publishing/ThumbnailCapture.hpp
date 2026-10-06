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
// currently holds into `outputPath` -- a PNG when the path ends in ".png"
// (what the catalogue accepts), otherwise an uncompressed PPM (P6).
// `colorImage` is expected to be in VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
// on entry and is left in that same layout on return, so a live
// ImGui::Image() display of the same target isn't disrupted by capturing it.
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
