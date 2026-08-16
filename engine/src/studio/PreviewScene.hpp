#pragma once

#include <volk.h>
#include <vk_mem_alloc.h>

#include "core/Camera.hpp"
#include "core/ECS.hpp"
#include "core/Mesh.hpp"
#include "core/ParticleSystem.hpp"
#include "core/Renderer.hpp"
#include "core/RiggedMesh.hpp"
#include "core/Texture.hpp"
#include "studio/OffscreenTarget.hpp"

namespace engine::studio {

// A small, self-contained "second 3D scene" -- its own ECS, orbit
// camera, and OffscreenTarget, rendered via the exact same
// core::Renderer::drawSceneInto() the main Viewport panel and
// engine_runtime both use (Principle 4: what a preview shows is what the
// real pipeline actually produces, not an approximation). Reused by both
// studio::AvatarPreviewer (a mannequin plus its equipped items) and
// studio::CataloguePanel's item-detail popup (one item, alone) rather
// than each hand-rolling its own offscreen-viewport plumbing.
//
// Shares the caller's MeshLibrary/TextureLibrary (previewed items'
// meshes/textures are registered there like anything else -- no separate
// GPU-resource pool per preview scene) but owns its own ECS, so its
// content never collides with, or gets listed in, the main scene's
// Explorer/Inspector, and its own ParticleSystem (real, but never
// populated with emitters here -- just what drawSceneInto()'s signature
// requires).
//
// core::Renderer only supports one PrePassCallback at a time (see its
// header comment), so a PreviewScene doesn't register its own --
// StudioApp's single pre-pass callback calls render() on every
// currently-open PreviewScene-owning panel once per frame, back to back,
// all within the one callback the main Viewport already uses. Each call
// is fully self-contained (its own images, its own layout transitions),
// so this composes safely with the main viewport render and any other
// PreviewScene doing the same thing in the same callback.
class PreviewScene {
public:
    // Call once per frame from StudioApp's pre-pass callback, only while
    // the owning panel is open (rendering a closed/invisible panel's
    // scene would be wasted GPU work with nothing to display it). Lazily
    // acquires a real core::Renderer::AuxiliarySceneHandle on first call
    // (see that type's doc comment for the real same-frame resource
    // collision it exists to avoid -- multiple PreviewScene instances,
    // and the main Viewport, can all render within the same frame's
    // command buffer precisely because each owns an independent handle
    // instead of sharing the Renderer's single per-frame-in-flight
    // scene state).
    // `riggedMeshLibrary` is optional (default nullptr, matching
    // core::Renderer::drawSceneInto()'s own trailing parameter) -- every
    // existing caller (AvatarPreviewer, CataloguePanel,
    // UploadAvatarItemPlugin) keeps compiling and behaving identically
    // unchanged; only a scene with real SkinnedRenderable entities (see
    // studio::plugins::AnimationPreviewerPlugin) needs to pass one.
    // Kronos ("Home Screen Avatar Preview" -- "cinematic lighting
    // preset"): `lightingOverride` is real, new, optional (default
    // nullptr, so every existing caller -- AvatarPreviewer, AvatarEditor,
    // CataloguePanel -- keeps rendering under this class's own flat
    // "studio lightbox" default, unchanged). A non-null pointer swaps in
    // the caller's own SceneLighting instead, the same real
    // swap-then-restore mechanism this class already uses internally for
    // its own default -- see .cpp.
    void render(VkCommandBuffer cmd, core::Renderer& renderer, core::MeshLibrary& meshLibrary,
                core::TextureLibrary& textureLibrary, core::RiggedMeshLibrary* riggedMeshLibrary = nullptr,
                const core::SceneLighting* lightingOverride = nullptr);

    // Draws the rendered texture into whatever ImGui window/child the
    // caller has already Begin()'d, sized to fill the available content
    // region, and handles orbit (left-drag) + zoom (scroll wheel) input
    // while the image is hovered. Same one-frame render/display latency
    // as the main Viewport panel (see OffscreenTarget.hpp's doc comment)
    // -- normal for a render-to-texture view, invisible at interactive
    // framerates.
    void drawAndHandleOrbit();

    [[nodiscard]] core::ECS& ecs() { return ecs_; }

    // Real, up-to-date (position/yaw/pitch recomputed at the end of every
    // drawAndHandleOrbit() call, see that method) read-only access to the
    // orbit camera -- lets a caller project its own world-space points
    // (e.g. studio::plugins::AnimationPreviewerPlugin's skeleton overlay)
    // into the exact same view the rendered texture was drawn with. Call
    // only after drawAndHandleOrbit() has run this frame.
    [[nodiscard]] const core::Camera& camera() const { return camera_; }

    // Sprint 10 ("Creator Tools Phase 2") task category 2's "live
    // preview window" for the Particle Editor -- previously private and
    // "never populated" (see class comment), now exposed so a caller
    // that *does* want a real, live-ticking particle emitter in its
    // preview (unlike AvatarPreviewer/CataloguePanel/MaterialPlugin,
    // which only ever preview static geometry) can call
    // ParticleSystem::update() against this scene's own ecs() each
    // frame. Every existing caller is unaffected -- an unpopulated,
    // un-ticked ParticleSystem behaves exactly as before.
    [[nodiscard]] core::ParticleSystem& particleSystem() { return particleSystem_; }

    // Clears every entity currently in this scene's ECS and resets the
    // orbit camera to its default framing -- "Reset Preview".
    void reset();

    // Releases both the OffscreenTarget and (if acquired) the
    // AuxiliarySceneHandle -- needs `renderer` for the latter, unlike the
    // allocator/device pair OffscreenTarget::destroy() alone needs.
    void destroy(core::Renderer& renderer, VmaAllocator allocator, VkDevice device);

private:
    core::ECS ecs_;
    core::Camera camera_;
    core::ParticleSystem particleSystem_; // real, never populated -- see class comment
    OffscreenTarget target_;
    VkExtent2D desiredExtent_{512, 512};
    core::Renderer::AuxiliarySceneHandle auxiliaryScene_ = core::Renderer::kInvalidAuxiliaryScene;

    // Orbit-camera state -- yaw/pitch/distance around a fixed focus
    // point, not a free-fly camera (ViewportPanel's own camera is
    // free-fly; a preview turntable orbits *around the subject*, the
    // standard "inspect this one object" control scheme). camera_'s
    // position/yaw/pitch are recomputed from this state at the end of
    // every drawAndHandleOrbit() call.
    glm::vec3 focusPoint_{0.0f, 1.0f, 0.0f};
    float orbitYawDegrees_ = -60.0f;
    float orbitPitchDegrees_ = -12.0f;
    float orbitDistance_ = 3.0f;
    static constexpr float kMinOrbitDistance = 0.5f;
    static constexpr float kMaxOrbitDistance = 15.0f;
};

} // namespace engine::studio
