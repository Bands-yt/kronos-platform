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
class MovieModePlugin;
class ModelImporterPlugin;
class ModelingModePlugin;
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
    // Kronos ("Clean Viewport & Mesh Import Pipeline" -- viewport "Add
    // Primitive" menu): real mesh handles for the 3 shapes that menu
    // offers beyond Cube (boxMesh, already above) -- Sphere/Torus/Cylinder
    // -- registered once by StudioApp the same way boxMesh/capsuleMesh
    // already are (see setPropSpawnMeshHandles()'s own comment). Cylinder
    // deliberately spawns with NO MeshSource (see Mesh::createCylinder()'s
    // own comment): it renders/moves/selects normally this session but
    // won't survive a Save Scene reload, the same accepted limitation
    // BlockBuilderPlugin's own Cylinder already has. Torus is a real
    // MeshSourceKind (see that enum's own comment) and does survive a
    // reload. sphereMesh is really a zero-half-height Capsule (radius,
    // 0) -- the same real, already-round-tripping "sphere" convention
    // BlockBuilderPlugin::spawnBlock() already established (MeshSourceKind
    // ::Capsule, not a dedicated Sphere kind).
    uint32_t sphereMesh = 0;
    uint32_t torusMesh = 0;
    uint32_t cylinderMesh = 0;
    uint32_t planeMesh = 0;
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
              const ViewportDebugContext& debugContext = ViewportDebugContext{},
              // Draws the camera-rail gizmo when non-null -- see
              // drawCameraRailOverlay().
              plugins::MovieModePlugin* movieMode = nullptr,
              // Kronos ("Modular Executable Targets" -- dedicated
              // workspace layouts): the third debug-overlay toolbar row
              // (Bounds/Terrain Streaming/CSM Cascades -- Sprint 8 task
              // category 2) is generic engine instrumentation, not part
              // of any of the 3 narrow apps' own named panel list.
              // Defaults true so Full/kronos_studio (and every existing
              // caller that doesn't pass this) keeps the exact same
              // toolbar it always has.
              bool showEngineDebugOverlays = true,
              // Kronos ("3D DCC Modeling Suite" -- real sub-object raycast
              // picking): when `selected` carries a real
              // core::EditableMeshComponent, drawSubObjectEditing() takes
              // over from drawGizmo() for that entity -- Ctrl+Click resolves
              // a real vertex/edge/face per this plugin's own
              // subObjectMode(), and a Translate-only gizmo manipulates the
              // result. nullptr (every existing caller) means Modeling
              // Mode's own sub-object picking/gizmo simply doesn't run,
              // same "absent plugin, absent feature" precedent
              // physicsPreview/movieMode above already establish.
              plugins::ModelingModePlugin* modelingMode = nullptr);

    [[nodiscard]] core::Camera& camera() { return camera_; }
    [[nodiscard]] const core::Camera& camera() const { return camera_; }

    // The camera pose actually used to render the texture currently on
    // screen (OffscreenTarget.hpp's own one-frame latency means that's
    // last frame's camera_, not this frame's just-updated one). Every
    // overlay (gizmo, selection highlight, grid, picking ray) projects
    // through this instead of camera_ directly, so they line up with the
    // displayed image instead of the one about to be rendered. Called
    // from StudioApp's pre-pass callback right where it hands camera_ to
    // drawSceneInto() -- takes whichever core::Camera was actually used
    // for that call (see the parameter's own note below), not always
    // camera_ directly.
    //
    // Kronos ("Cinema Rigs" -- live DoF preview): on a frame where
    // StudioApp substitutes MovieModePlugin's rail camera for the
    // free-fly camera_ (see that call site's own comment), the caller
    // passes that substituted camera here instead, so every overlay
    // above keeps projecting through the same pose as the displayed
    // image rather than silently reverting to the free-fly camera's.
    void snapshotRenderCamera(const core::Camera& usedForRender) { renderCamera_ = usedForRender; }

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

    // Kronos ("Clean Viewport & Mesh Import Pipeline"): real, deferred
    // setter, same shape as setPropSpawnMeshHandles() above -- StudioApp
    // calls this only for the modes the brief actually names ("Import 3D
    // Asset..." buttons and Add Primitive menus... for 3D Maker and
    // Studio"), so Movie Maker/Audio's own Viewport (Movie Maker's, since
    // Audio has none) never grows this toolbar row at all. `modelImporter`
    // may be nullptr (narrow modes that enable this still only register
    // MaterialPlugin/MeshCsgWindowPlugin, not ModelImporterPlugin) -- the
    // "Import 3D Asset..." button itself just doesn't draw in that case,
    // same "real, honest, absent control beats a button that does
    // nothing" precedent every other nullptr-gated control in this file
    // already follows.
    void setAssetTools(bool enabled, plugins::ModelImporterPlugin* modelImporter) {
        showAssetTools_ = enabled;
        modelImporterPlugin_ = modelImporter;
    }

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
    // Camera-path spline, control-point handles and look-at vectors for
    // Movie Mode's rail -- see the .cpp for why this lives here rather
    // than in the plugin.
    void drawCameraRailOverlay(plugins::MovieModePlugin& movieMode, ImVec2 imageOrigin, ImVec2 imageSize);
    // Which rail control point a drag is moving; -1 when none is.
    int draggingRailPoint_ = -1;

    // Kronos ("3D DCC Modeling Suite" -- real sub-object raycast picking):
    // real Ctrl+Click ray-vs-mesh picking (via `modelingMode`'s own
    // pickSubObject(), same real core::pickTriangleUv() Moller-Trumbore
    // test MaterialPlugin's viewport click-to-paint already proved),
    // a real highlight (a screen-projected point/line/triangle over the
    // actual selected vertex/edge/face) and a real Translate-only
    // ImGuizmo anchored at subObjectAnchorLocal() -- transformed to world
    // space by `selected`'s own Transform, mirroring drawGizmo()'s own
    // view/proj/model setup. Draws INSTEAD of drawGizmo() for this entity
    // (see draw()'s own call site) -- an Object-mode move gizmo and a
    // sub-object one at a different anchor would otherwise overlap and
    // fight for the same drag.
    void drawSubObjectEditing(plugins::ModelingModePlugin& modelingMode, core::ECS& ecs, core::EntityId selected,
                               ImVec2 imageOrigin, ImVec2 imageSize);

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

    // Kronos ("Clean Viewport & Mesh Import Pipeline" -- "clean ground
    // grid"): a real, always-on world-space grid of lines on the XZ plane
    // at y=0, projected with the exact same worldToScreen() helper every
    // other overlay above already uses -- not a texture/shader change,
    // just screen-space lines drawn every frame the same way physics/
    // terrain debug wireframes already are. Deliberately unconditional
    // (no showX_ toggle): this is meant to always read as "an empty
    // studio floor", the same permanent baseline a real 3D editor's
    // viewport grid is, not an opt-in debug overlay.
    void drawGroundGridOverlay(ImVec2 imageOrigin, ImVec2 imageSize);

    core::Camera camera_;
    // See snapshotRenderCamera()'s own comment. Default-constructed to
    // the same values as camera_ so the very first frame (before any
    // snapshot is taken) still matches.
    core::Camera renderCamera_;
    bool dragging_ = false;
    VkExtent2D desiredExtent_{0, 0};
    GizmoOperation gizmoOperation_ = GizmoOperation::Translate;
    GizmoSpace gizmoSpace_ = GizmoSpace::World;
    // Adobe-style Beginner/Advanced toggle for the left tool column.
    // Beginner hides gizmo-space + snap controls and the physics/debug-
    // overlay rows entirely. Defaults true (Advanced) so nothing anyone's
    // already using disappears without them choosing Beginner first.
    bool advancedMode_ = true;
    // Kronos ("Studio Asset Drag-and-Drop"): see setPropSpawnMeshHandles()'s
    // own comment. propSpawnCount_ is this panel's own real, independent
    // counter (spawnPropAuthoring()'s own `spawnIndex` -> "Tree 3" naming)
    // -- deliberately not shared with CreatorAssetBrowserPlugin's own
    // "Use" button counter, since the two are real, separate spawn
    // origins; a real name collision is cosmetic, not a correctness bug
    // (core::ECS entity ids, not names, are the real identity).
    WorldPropSpawnMeshHandles propSpawnMeshHandles_;
    // See setAssetTools()'s own comment.
    bool showAssetTools_ = false;
    plugins::ModelImporterPlugin* modelImporterPlugin_ = nullptr;
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
