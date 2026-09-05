#pragma once

#include <glm/glm.hpp>

#include "core/Camera.hpp"
#include "core/Mesh.hpp"
#include "studio/IKronosPlugin.hpp"
#include "studio/SecondaryViewport.hpp"

namespace engine::studio::plugins {

// Kronos ("3D Mesh & CSG Editor" -- Beta Roadmap Phase 2, "windowed
// plugin module"): a real, standalone-window live viewport onto the
// exact same core::ECS studio::plugins::ModelingModePlugin already
// edits (vertex/edge/face tools + CSG, via core::EditableMeshComponent/
// core::CsgMesh -- see that plugin's own header comment). This class
// deliberately owns NO editing logic of its own -- ModelingModePlugin's
// existing docked ImGui panel is still where "Start Editing"/CSG/
// extrude buttons live; this is only the window/swapchain/camera shell
// around it.
//
// Bi-directional sync with the ECS is real but structural, not an
// actively-written feature: renderFrame() draws whatever `ecs` looks
// like *this frame*, and ModelingModePlugin::reuploadMesh() already
// re-uploads a real GPU core::Mesh into the exact same MeshLibrary
// handle after every edit (its own update()'s editVersion sweep covers
// script-driven edits too) -- there is no second copy of the geometry
// for this window to diff or reconcile, so an edit made through the
// main window's panel is visible here the very next renderFrame() call
// with no extra plumbing, and this window has no controls that could
// push a change the other direction.
class MeshCsgWindowPlugin final : public IKronosPlugin {
public:
    MeshCsgWindowPlugin(core::MeshLibrary& meshLibrary, core::TextureLibrary& textureLibrary)
        : meshLibrary_(&meshLibrary), textureLibrary_(&textureLibrary) {}

    [[nodiscard]] const char* name() const override { return "3D Mesh & CSG Editor"; }

    void tick(float dt, core::ECS& ecs, core::EntityId selected) override;
    void renderFrame(core::Renderer& renderer, core::ECS& ecs, core::EntityId selected) override;
    void handleEvent(const SDL_Event& event) override;

protected:
    void onOpen(core::Renderer& renderer) override;
    void onClose(core::Renderer& renderer) override;

private:
    core::MeshLibrary* meshLibrary_;
    core::TextureLibrary* textureLibrary_;
    SecondaryViewport viewport_;
    core::Camera camera_;

    // Real, simple orbit-drag camera (left mouse drag to orbit, wheel to
    // zoom) -- independent of the main Viewport's own free-fly camera,
    // and re-centered on `selected` every tick (see .cpp) so opening
    // this window while an entity is selected in Explorer frames it
    // immediately, matching this codebase's other camera-rig
    // conveniences (e.g. studio::PreviewScene's own auto-frame).
    glm::vec3 orbitTarget_{0.0f};
    float orbitYawDegrees_ = -45.0f;
    float orbitPitchDegrees_ = -20.0f;
    float orbitDistance_ = 6.0f;
};

} // namespace engine::studio::plugins
