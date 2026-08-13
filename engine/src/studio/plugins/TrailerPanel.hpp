#pragma once

#include <string>
#include <vector>

#include <volk.h>
#include <vk_mem_alloc.h>

#include "core/Camera.hpp"
#include "core/Renderer.hpp"
#include "net/NetworkSession.hpp"
#include "studio/IStudioPlugin.hpp"
#include "studio/ThumbnailCameraRig.hpp"
#include "trailer/TrailerDirector.hpp"

namespace engine::studio::plugins {

// Sprint 15 ("TNT-Wars Trailer Production")'s real Studio-side authoring
// tool: a real trailer::TrailerDirector (the exact same class
// engine_runtime's own --trailer mode drives, see that class's own
// header comment) driving its own real, dedicated core::Camera -- NOT
// the main Viewport's edit camera, so a creator scrubbing/previewing the
// trailer doesn't fight their own edit view. The live preview reuses
// Sprint 13's own real studio::ThumbnailCameraRig (the exact same
// AuxiliarySceneHandle + live-render + ImGui::Image pattern
// PublishingPanel already established for "render this off to the side,
// live, without disturbing the main Viewport"); the real captured-frame-
// to-disk sequence is written by director_'s own internal
// trailer::CaptureRig (the non-ImGui rig engine_runtime uses too) --
// two separate real offscreen targets serving two separate real
// purposes (on-screen scrub preview vs. real file output), both driven
// from the same real camera/beat state every frame.
class TrailerPanel final : public IStudioPlugin {
public:
    TrailerPanel(VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue, core::ECS& ecs,
                 core::MeshLibrary& meshLibrary, core::TextureLibrary& textureLibrary, core::ParticleSystem& particleSystem,
                 net::NetworkSession& session);

    [[nodiscard]] const char* name() const override { return "TNT-Wars Trailer"; }
    [[nodiscard]] const char* category() const override { return "World"; }

    // Real, runs every frame regardless of open() (see IStudioPlugin's
    // own class comment) -- advances director_ by dt only while
    // playing_/capturing, so a closed panel doesn't silently keep the
    // trailer running in the background.
    void update(float dt, core::ECS& ecs, core::EntityId selected, const std::vector<core::EntityId>& selectedEntities) override;
    void drawPanel(core::ECS& ecs, core::EntityId selected, const std::vector<core::EntityId>& selectedEntities) override;

    // Real render hook for both real offscreen targets -- called once
    // per frame from StudioApp's pre-pass callback, only while this
    // panel is open (matches PublishingPanel's own real convention).
    void renderPreview(VkCommandBuffer cmd, core::Renderer& renderer, core::ECS& ecs);
    void shutdown(core::Renderer& renderer);

private:
    void drawSceneListSection();
    void drawPlaybackSection();
    void drawCaptureSection();

    core::MeshLibrary* meshLibrary_;
    core::TextureLibrary* textureLibrary_;

    // Declared before director_ on purpose -- trailer::TrailerDirector's
    // constructor takes a real Camera& to this, and C++ member
    // initialization runs in declaration order regardless of initializer-
    // list order.
    core::Camera trailerCamera_;
    trailer::TrailerDirector director_;

    ThumbnailCameraRig previewRig_;
    bool captureInitialized_ = false;
    bool playing_ = false;
    // Real, cached once renderPreview() first runs with a live Renderer&
    // -- update() (unlike renderPreview()) genuinely runs every real
    // frame regardless of this panel's open/closed state (see
    // IStudioPlugin's own class comment), so a real in-progress capture
    // keeps real-advancing even if a creator closes this panel mid-run,
    // rather than silently freezing.
    core::Renderer* cachedRenderer_ = nullptr;

    char outputDirBuffer_[256] = "trailer_output";
    float captureFps_ = 24.0f;

    std::vector<std::string> log_;
    void logLine(const std::string& line);
};

} // namespace engine::studio::plugins
