#pragma once

#include <string>

#include <volk.h>
#include <vk_mem_alloc.h>

#include "core/Camera.hpp"
#include "core/ECS.hpp"
#include "core/ParticleSystem.hpp"
#include "core/Renderer.hpp"
#include "studio/OffscreenTarget.hpp"

namespace engine::studio {

// Sprint 13 ("Publishing & Game Packaging") task 3's "Add a thumbnail
// camera" -- a real, independently-positionable camera (NOT the main
// Viewport's edit camera) that renders the *live* Studio ECS (passed by
// reference every call, unlike studio::PreviewScene, which deliberately
// owns its own private, isolated ECS for previewing a draft item in
// isolation -- see that class's header comment). A thumbnail needs to
// show the actual world being published, from a real, creator-composed
// angle, without disturbing their edit view.
//
// Structurally a slimmed-down mirror of PreviewScene (same real
// AuxiliarySceneHandle + OffscreenTarget pattern, see that class's .cpp
// for the exact precedent this one follows), with two real differences:
// render() takes an external core::ECS& instead of owning one, and
// `camera` is real, direct creator-adjustable state (position/yaw/
// pitch), not orbit-drag-only.
class ThumbnailCameraRig {
public:
    void render(VkCommandBuffer cmd, core::Renderer& renderer, core::ECS& ecs, core::MeshLibrary& meshLibrary,
                core::TextureLibrary& textureLibrary);

    // Real GPU readback of whatever the last render() call produced --
    // see publishing::captureThumbnailToFile()'s own header comment for
    // the PPM format choice. Returns false (logged) if render() was
    // never called successfully first.
    [[nodiscard]] bool captureToFile(core::Renderer& renderer, const std::string& outputPath) const;

    void destroy(core::Renderer& renderer, VmaAllocator allocator, VkDevice device);

    [[nodiscard]] bool hasRenderedFrame() const { return target_.isValid(); }
    [[nodiscard]] VkDescriptorSet imguiTextureId() const { return target_.imguiTextureId(); }
    [[nodiscard]] VkExtent2D extent() const { return target_.extent(); }

    // Real, direct creator-adjustable pose -- a studio::plugins::PublishingPanel
    // exposes this via plain sliders (position X/Y/Z, yaw, pitch), not
    // orbit-drag, so a creator can compose and reproduce a specific real
    // shot rather than fiddling with a mouse each time.
    core::Camera camera;
    VkExtent2D desiredExtent{512, 512};

private:
    OffscreenTarget target_;
    core::Renderer::AuxiliarySceneHandle auxiliaryScene_ = core::Renderer::kInvalidAuxiliaryScene;
    core::ParticleSystem particleSystem_; // real, never populated -- required by drawSceneInto()'s signature only, same as PreviewScene
};

} // namespace engine::studio
