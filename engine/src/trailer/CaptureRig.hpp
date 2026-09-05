#pragma once

#include <string>
#include <vector>

#include <volk.h>
#include <vk_mem_alloc.h>

#include "cinematic/CameraRail.hpp"
#include "cinematic/OfflineExport.hpp"
#include "cinematic/Sequencer.hpp"
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

    // Real end-to-end Offline Export: walks `sequence`/`rail`'s real
    // schedule (cinematic::runExportSchedule(), built from `settings` via
    // cinematic::buildExportSchedule() -- the exact same function
    // ScriptCinematicApi's `cinematic.buildExportSchedule()` and Studio's
    // "Render Movie Sequence" button already call for their own dry-run
    // preview), and for every sub-frame sample does a real render
    // (drawSceneInto) + real GPU readback into memory, accumulates
    // motion-blur sub-frames, and writes real PNG (Color, via stb_image_
    // write) and/or EXR (Depth, via tinyexr) files to
    // `settings.outputDirectory` using cinematic::exportFrameFilename()
    // for real, collision-free names.
    //
    // Headless safety: returns false immediately (logged, `outError`
    // filled) when this rig isn't isValid() rather than dereferencing a
    // null image -- the honest "no GPU swapchain" branch (see this
    // header's own file comment on why this is never called from
    // engine_tests, matching CaptureRig::captureFrame()'s own existing
    // convention).
    [[nodiscard]] bool exportSequence(core::Renderer& renderer, core::ECS& ecs, core::MeshLibrary& meshLibrary,
                                       core::TextureLibrary& textureLibrary, cinematic::Sequence& sequence,
                                       cinematic::CameraRail& rail, const cinematic::ExportSettings& settings,
                                       std::string& outError);

    [[nodiscard]] VkExtent2D extent() const { return extent_; }
    [[nodiscard]] bool isValid() const { return colorImage_ != VK_NULL_HANDLE; }

private:
    // Real, self-contained one-shot render + GPU readback into memory
    // (unlike captureFrame(), which reads back straight to a PPM file via
    // publishing::captureThumbnailToFile()) -- exportSequence()'s own
    // real per-sample capture path, kept private since nothing outside
    // this class has a use for a raw in-memory frame. `outDepth` is only
    // filled when `captureDepth` is true; a null/empty result otherwise
    // is a real, honest "not requested", not a failure.
    [[nodiscard]] bool renderAndReadback(core::Renderer& renderer, core::ECS& ecs, core::MeshLibrary& meshLibrary,
                                          core::TextureLibrary& textureLibrary, const core::Camera& camera,
                                          bool captureDepth, std::vector<uint8_t>& outColorRgba8,
                                          std::vector<float>& outDepth);

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
