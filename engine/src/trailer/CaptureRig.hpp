#pragma once

#include <string>

#include <volk.h>
#include <vk_mem_alloc.h>

#include "core/Camera.hpp"
#include "core/ECS.hpp"
#include "core/ParticleSystem.hpp"
#include "core/Renderer.hpp"

namespace engine::trailer {

// Sprint 15 ("TNT-Wars Trailer Production")'s real, engine_runtime-side
// capture target -- structurally the same real AuxiliarySceneHandle +
// offscreen color/depth image pattern studio::ThumbnailCameraRig/
// studio::OffscreenTarget already established (see those classes' own
// header comments for the precedent), deliberately NOT reusing
// studio::OffscreenTarget directly: that class registers an
// ImGui_ImplVulkan_AddTexture descriptor for docked-panel display, and
// engine_runtime has no ImGui backend at all -- calling into it here
// would be a real, immediate crash. This is the same real color+depth
// image creation, minus the ImGui half, plus fixed-size (a trailer
// capture's resolution is chosen once for the whole run, not resized
// live like the Viewport panel is).
class CaptureRig {
public:
    ~CaptureRig();

    // Real, one-time setup at `extent`, using `renderer.swapchainFormat()`
    // for the color image (the real scene pipeline is format-specific --
    // see studio::OffscreenTarget::ensureSize()'s own comment for why).
    // Call once before the first real captureFrame().
    [[nodiscard]] bool initialize(core::Renderer& renderer, VkExtent2D extent);
    void shutdown(core::Renderer& renderer);

    // Real render + real GPU readback + real numbered PPM file write, in
    // one self-contained call (its own real one-shot command buffer,
    // submitted and waited on internally -- matching
    // publishing::captureThumbnailToFile()'s own real one-shot
    // convention, see that function's header comment): draws `ecs` from
    // `camera` into this rig's own real offscreen target
    // (core::Renderer::drawSceneInto()), then real-reads it back into
    // `outputDirectory/<frameFilename(frameIndex)>`. Returns false
    // (logged) on any real failure.
    [[nodiscard]] bool captureFrame(core::Renderer& renderer, core::ECS& ecs, core::MeshLibrary& meshLibrary,
                                     core::TextureLibrary& textureLibrary, const core::Camera& camera,
                                     const std::string& outputDirectory, int frameIndex);

    [[nodiscard]] VkExtent2D extent() const { return extent_; }
    [[nodiscard]] bool isValid() const { return colorImage_ != VK_NULL_HANDLE; }

private:
    VkExtent2D extent_{0, 0};
    VkFormat colorFormat_ = VK_FORMAT_UNDEFINED;

    VkImage colorImage_ = VK_NULL_HANDLE;
    VmaAllocation colorAllocation_ = nullptr;
    VkImageView colorImageView_ = VK_NULL_HANDLE;

    VkImage depthImage_ = VK_NULL_HANDLE;
    VmaAllocation depthAllocation_ = nullptr;
    VkImageView depthImageView_ = VK_NULL_HANDLE;

    core::Renderer::AuxiliarySceneHandle auxiliaryScene_ = core::Renderer::kInvalidAuxiliaryScene;
    // Real, never-populated -- required only by drawSceneInto()'s
    // signature, same real "particle system this rig never feeds"
    // convention studio::ThumbnailCameraRig's own particleSystem_ member
    // already established.
    core::ParticleSystem captureParticleSystem_;
};

// Real, pure, deterministic filename generation -- exposed separately
// from captureFrame() so it's real, headless-testable (no GPU needed):
// six-digit zero-padded frame index, enough real headroom for a real
// multi-thousand-frame trailer capture without the lexical (string) sort
// order of a directory listing ever disagreeing with real numeric frame
// order.
[[nodiscard]] std::string frameFilename(int frameIndex);

} // namespace engine::trailer
