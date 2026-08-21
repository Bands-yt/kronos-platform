#pragma once

#include <vector>

#include <imgui.h>
#include <volk.h>

#include "core/Camera.hpp"
#include "core/ECS.hpp"
#include "studio/panels/ExplorerPanel.hpp"

namespace engine::core {
class MeshLibrary;
class Renderer;
class Terrain;
}

namespace engine::studio::plugins {
class PhysicsPreviewPlugin;
}

namespace engine::studio::panels {

// Sprint 8 ("Performance Stats & Debug Tools") task category 2: the
// terrain-streaming and CSM-cascade overlays need real live systems
// ViewportPanel doesn't otherwise touch (a real core::Terrain, a real
// core::Renderer for computeCascades()) -- grouped into one small,
// optional struct rather than three more trailing draw() parameters, the
// same "small real struct beats a long positional parameter list" reasoning
// WorldPropSpawnInfo already established. All-null/zero (the default) is
// a real, honest "no terrain, no cascade data available" configuration,
// not an error -- the corresponding overlays just don't draw anything.
struct ViewportDebugContext {
    core::Terrain* terrain = nullptr;
    float terrainLoadRadius = 0.0f;
    float terrainUnloadRadius = 0.0f;
    core::Renderer* renderer = nullptr; // for CSM cascade visualization
};

// Kronos ("Studio Asset Drag-and-Drop"): real mesh handles ViewportPanel
// needs to spawn a real authoring prop entity (studio::spawnPropAuthoring(),
// same real function CreatorAssetBrowserPlugin's own "Use" button already
// calls) when a real prop entry is dragged in from the Asset Browser and
// dropped -- see setPropSpawnMeshHandles()'s own comment for why these
// come in via a deferred setter, not a constructor parameter.
struct WorldPropSpawnMeshHandles {
    uint32_t boxMesh = 0;
    uint32_t capsuleMesh = 0;
};

// docs/ARCHITECTURE.md §4.2's Edit Viewport: "The editor camera is a
// free-fly, unreplicated entity feeding the same [render] graph; it never
// enters the ECS as a simulated object." Uses core::Camera directly
// (shared with engine_runtime, see Camera.hpp) rather than a parallel
// camera type, since the matrix math is identical -- only who drives
// position/yaw/pitch differs (this class's mouse-look, vs. eventually a
// gameplay script).
//
// draw() displays the *real* rendered scene -- studio/OffscreenTarget.hpp
// is what StudioApp renders it into, one frame before it's displayed here
// (see that class's doc comment on why that latency is fine and
// deliberate). This panel doesn't touch Renderer/Vulkan directly beyond
// that: it takes whatever ImTextureID StudioApp hands it, draws an
// ImGui::Image, overlays the real ImGuizmo translate/rotate/scale
// manipulator on top of it for the current selection (§4.2: "an
// immediate-mode overlay (ImGuizmo-style)" -- this uses the actual
// library, not an approximation), and reports back the pixel size it
// wants for the *next* frame's render via desiredExtent().
//
// Also owns real click-to-select and drag-select-box picking (see
// core::pickEntity(), ScenePicking.hpp) -- a plain left-click raycasts
// from the camera through the cursor and selects whatever's closest,
// ctrl-click toggles it into/out of the multi-select, and a click-drag
// past a small pixel threshold draws a screen-space selection rectangle
// and selects every entity whose position projects inside it on release.
// Both write directly into the passed-in ExplorerPanel (the single owner
// of selection state -- see its header comment) rather than returning a
// result StudioApp has to interpret and forward, the same reasoning
// setSelected()/toggleSelection()/setSelectedMultiple() exist on that
// class for.
class ViewportPanel {
public:
    enum class GizmoOperation { Translate, Rotate, Scale };
    enum class GizmoSpace { Local, World };

    // `physicsPreview` is optional (nullptr outside a context that has one)
    // -- when present, draws the physics debug-draw overlay (task category
    // 4) and its viewport toolbar toggles on top of the scene image, using
    // that plugin's showColliders/showContacts/showRaycasts flags,
    // recentContacts(), and castTestRay()/testRayHit(). Its
    // recentContacts() reflects the *previous* frame's physics step (see
    // StudioApp::run()'s ordering -- pluginManager_.update() runs after
    // this draw() call), the same one-frame latency OffscreenTarget.hpp
    // already documents as fine and deliberate for the scene image itself.
    // `debugContext` (task category 2, new this pass) draws the terrain-
    // streaming and CSM-cascade debug overlays and their own viewport
    // toolbar toggles when its fields are non-null -- see
    // ViewportDebugContext's own comment.
    void draw(float deltaTime, VkDescriptorSet sceneTexture, VkExtent2D sceneTextureExtent, core::ECS* ecs,
              core::MeshLibrary* meshLibrary, ExplorerPanel& explorer,
              plugins::PhysicsPreviewPlugin* physicsPreview = nullptr,
              const ViewportDebugContext& debugContext = ViewportDebugContext{});

    [[nodiscard]] core::Camera& camera() { return camera_; }
    [[nodiscard]] const core::Camera& camera() const { return camera_; }

    // What size (in pixels) this panel's content region was at the end of
    // the most recent draw() call -- what StudioApp resizes the
    // OffscreenTarget to before the *next* frame's scene render.
    [[nodiscard]] VkExtent2D desiredExtent() const { return desiredExtent_; }

    void setGizmoOperation(GizmoOperation op) { gizmoOperation_ = op; }
    [[nodiscard]] GizmoOperation gizmoOperation() const { return gizmoOperation_; }

    // Kronos ("Studio Asset Drag-and-Drop"): real, deferred setter --
    // same "constructed before GPU is ready, real mesh handles arrive
    // once it is" shape core::Application::setOreDropMeshHandle()/
    // setScriptSpawnBoxMeshHandle() already establish. Called once from
    // StudioApp, right alongside CreatorAssetBrowserPlugin's own
    // identical real box/capsule mesh registration, so a dropped prop's
    // visual is never a stale/invalid handle.
    void setPropSpawnMeshHandles(WorldPropSpawnMeshHandles handles) { propSpawnMeshHandles_ = handles; }

private:
    void updateFreeFly(float deltaTime);
    // Sprint 9 ("Creator Tools Phase 1") task category 2's "group move":
    // `allSelected` is the full multi-select set (see ExplorerPanel's own
    // comment on what that means) -- while translating (Rotate/Scale
    // group operations are deliberately out of scope, see this method's
    // own implementation comment), every *other* selected entity's
    // Transform::position shifts by the same real delta the gizmo just
    // applied to the primary selection, preserving each entity's own
    // relative offset rather than snapping them all to one point.
    void drawGizmo(core::ECS& ecs, core::EntityId selected, const std::vector<core::EntityId>& allSelected,
                    ImVec2 imageOrigin, ImVec2 imageSize);
    // Kronos ("Studio UI/UX Friendliness" -- "better visual feedback for
    // selected entities"): real, screen-space projected wireframe box
    // around every real selected entity's own world-space mesh AABB
    // (Mesh::localBoundsMin/Max, the same real bounds ScenePicking.hpp's
    // own ray-vs-AABB test already uses) -- distinct from, and drawn
    // alongside, the gizmo itself (a gizmo shows *how to move* the
    // selection; this shows *what's actually selected*, which matters
    // most for a small or gizmo-occluded entity, or a multi-selection
    // where only the primary entity gets a gizmo at all).
    void drawSelectionHighlight(core::ECS& ecs, core::MeshLibrary& meshLibrary,
                                 const std::vector<core::EntityId>& selectedEntities, ImVec2 imageOrigin,
                                 ImVec2 imageSize);
    // Ray in world space (origin, normalized direction) through `mousePos`
    // -- real inverse-view-projection unprojection at the mouse's NDC
    // position, not an approximation. Used by both click-to-select and
    // (indirectly, via the same camera matrices) the drag-select box's
    // screen-space projection test.
    void computeMouseRay(ImVec2 mousePos, ImVec2 imageOrigin, ImVec2 imageSize, glm::vec3& outOrigin,
                          glm::vec3& outDirection) const;
    void handleSelection(core::ECS& ecs, core::MeshLibrary& meshLibrary, ExplorerPanel& explorer, ImVec2 imageOrigin,
                          ImVec2 imageSize);

    // Kronos ("Developer Velocity Sprint" -- "Drop-to-Ground Shortcut
    // (End Key)"): a real, physics-independent downward raycast
    // (core::pickEntity(), the same one click-to-select already uses --
    // Studio runs no live core::Physics outside Play mode, see that
    // function's own header comment) from `selected`'s current Transform
    // position, excluding `selected` itself. On a real hit, repositions
    // `selected` so its own mesh's local-space bottom (scaled by
    // Transform::scale.y, ignoring rotation -- a real, stated scope
    // simplification, see this method's own .cpp comment) rests exactly
    // on the hit surface, not its raw origin (which would sink a
    // center-origin mesh like a Box halfway into the ground). A real,
    // honest no-op if there's nothing selected or nothing below it.
    void dropSelectedToGround(core::ECS& ecs, core::MeshLibrary& meshLibrary, core::EntityId selected);

    // World -> screen projection shared by every debug-draw shape below --
    // same convention handleSelection()'s drag-select rectangle test
    // already uses (NDC from view*proj, then flip Y for screen space).
    // Returns false (and leaves outScreen untouched) for a point behind
    // the camera, which callers use to skip drawing that segment/marker
    // rather than draw a garbage line back through the camera.
    [[nodiscard]] bool worldToScreen(const glm::mat4& viewProj, glm::vec3 worldPos, ImVec2 imageOrigin,
                                      ImVec2 imageSize, ImVec2& outScreen) const;
    // Physics Debug Tools (task category 4): collider wireframes (Box/
    // Sphere/Capsule -- Mesh colliders are skipped, see
    // PhysicsPreviewPlugin's own comment on why Mesh has no live Play-mode
    // body to draw a wireframe *from* in the first place), recent contact
    // point/normal markers, and the on-demand test raycast -- each gated
    // independently by physicsPreview's own showColliders/showContacts/
    // showRaycasts flags so a user can enable just the one they need.
    void drawPhysicsDebugOverlay(core::ECS& ecs, plugins::PhysicsPreviewPlugin& physicsPreview, ImVec2 imageOrigin,
                                  ImVec2 imageSize);

    // Sprint 8 ("Performance Stats & Debug Tools") task category 2:
    // bounding-box overlay (every Renderable+Transform+MeshSource entity's
    // real local AABB, Mesh::localBoundsMin()/Max(), transformed by its
    // own Transform), terrain-streaming overlay (a wireframe box per
    // currently-loaded chunk from Terrain::chunkDebugInfo(), plus two
    // real load/unload-radius rings centered on the camera), and CSM
    // cascade overlay (a boundary rectangle at each real cascade split
    // depth from Renderer::computeCascades(), perpendicular to the
    // camera's forward direction) -- each independently gated by its own
    // toggle below, same "one flag per overlay kind" pattern
    // drawPhysicsDebugOverlay()'s showColliders/showContacts/showRaycasts
    // already established.
    void drawSprint8DebugOverlays(core::ECS& ecs, core::MeshLibrary& meshLibrary, const ViewportDebugContext& debugContext,
                                   ImVec2 imageOrigin, ImVec2 imageSize);

    core::Camera camera_;
    bool dragging_ = false;
    VkExtent2D desiredExtent_{0, 0};
    GizmoOperation gizmoOperation_ = GizmoOperation::Translate;
    GizmoSpace gizmoSpace_ = GizmoSpace::World;
    // Kronos ("Studio Asset Drag-and-Drop"): see setPropSpawnMeshHandles()'s
    // own comment. propSpawnCount_ is this panel's own real, independent
    // counter (spawnPropAuthoring()'s own `spawnIndex` -> "Tree 3" naming)
    // -- deliberately not shared with CreatorAssetBrowserPlugin's own
    // "Use" button counter, since the two are real, separate spawn
    // origins; a real name collision is cosmetic, not a correctness bug
    // (core::ECS entity ids, not names, are the real identity).
    WorldPropSpawnMeshHandles propSpawnMeshHandles_;
    int propSpawnCount_ = 0;
    // Kronos ("Developer Velocity Sprint" -- "Grid & Rotation Snapping"):
    // split from one combined toggle into two independent ones -- Grid
    // Snap (translate) and Angle Snap (rotate) are real, separately
    // stated toolbar controls, not one flag that happens to apply to
    // whichever gizmo mode is currently active. Scale keeps its own
    // separate toggle (scaleSnapEnabled_) -- the sprint's own ask names
    // only Grid/Angle Snap, so Scale's existing free-form control is
    // left exactly as it was.
    bool gridSnapEnabled_ = false;
    bool angleSnapEnabled_ = false;
    bool scaleSnapEnabled_ = false;
    float translateSnap_ = 1.0f;
    float rotateSnapDegrees_ = 15.0f;
    float scaleSnap_ = 0.1f;

    // Drag-select-box state -- see handleSelection()'s implementation for
    // the click-vs-drag disambiguation (a small pixel-movement threshold).
    bool dragSelectActive_ = false;
    ImVec2 dragSelectStart_{0.0f, 0.0f};

    // Sprint 8 debug overlay toggles -- default off, same "opt-in, not
    // drawn by default" convention the physics debug toggles use.
    bool showBoundingBoxes_ = false;
    bool showTerrainStreaming_ = false;
    bool showCascades_ = false;
};

} // namespace engine::studio::panels
