#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include <volk.h>
#include <vk_mem_alloc.h>

#include <glm/glm.hpp>

#include "core/Components.hpp"
#include "core/ComputePbrPainter.hpp"
#include "core/CsgMesh.hpp"
#include "core/EditableMeshComponent.hpp"
#include "core/Mesh.hpp"
#include "studio/IStudioPlugin.hpp"

namespace engine::studio::plugins {

// Kronos ("3D Model Maker" Phase 2 -- real vertex/edge/face editing):
// the Studio-facing half of core::EditableMesh (see that header's own
// comment for the pure topology-editing logic itself, which has zero
// ImGui/Vulkan dependency and is headlessly tested). This plugin owns
// the Vulkan-coupled part: turning a selected entity's mesh into a real,
// editable core::EditableMeshComponent, and re-uploading it to the GPU
// (MeshLibrary::replaceMesh) after every real edit so what's on screen
// always matches the current topology.
//
// Real, honest scope: "Start Editing" only works on an entity whose
// current MeshSource is a Box (Block Builder's Cube, or a hand-placed
// Creator Tools prop) -- EditableMesh::createBox() is the one real seed
// this plugin knows how to build that starts out visually identical to
// what's already there. Sphere/Cylinder/Wedge/Obj-imported meshes aren't
// editable yet (no seed constructor exists for them) -- said plainly in
// the panel itself, not silently disabled with no explanation.
//
// Selection is real but index-based (a real face/edge list with real
// centroid/position previews to click), not live 3D click-picking in the
// viewport -- core::pickEntity() (ScenePicking.hpp) only does whole-entity
// picking today, and building real ray-vs-triangle/ray-vs-edge picking
// for sub-mesh elements is real, separate scope this plugin doesn't
// attempt to smuggle in. A future pass can add it without changing
// anything below -- the operations themselves only need a face/edge
// index, however that index was chosen.
//
// Also owns the CSG panel (core::booleanOp(), CsgMesh.hpp) and the real
// re-upload sweep (update(), below) that makes core::ScriptMeshApi's
// Luau-driven edits (Studio's Debug Console) visible too, not just this
// panel's own buttons -- both are Beta Roadmap "Live Collaboration &
// In-Studio 3D Modeling Pipeline" additions.
class ModelingModePlugin final : public IStudioPlugin {
public:
    ModelingModePlugin(VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue,
                        core::MeshLibrary& meshLibrary);

    [[nodiscard]] const char* name() const override { return "Modeling Mode"; }
    [[nodiscard]] const char* category() const override { return "World"; }

    // Kronos ("Live Collaboration & In-Studio 3D Modeling Pipeline" --
    // Beta Roadmap): runs every frame regardless of whether the panel is
    // open (IStudioPlugin's own convention) -- sweeps every entity with
    // both a real EditableMeshComponent and a real Renderable, and
    // re-uploads any whose editVersion has moved past what this plugin
    // last uploaded. This is what makes a core::ScriptMeshApi-driven
    // edit (Studio's Debug Console, `mesh.*`) show up in the viewport
    // without needing this panel open -- see EditableMeshComponent::
    // editVersion's own header comment. drawPanel()'s own buttons below
    // still call reuploadMesh() directly for zero-latency feedback and
    // never touch editVersion, so this sweep is a real no-op for a
    // UI-only edit, not a redundant double-upload.
    void update(float dt, core::ECS& ecs, core::EntityId selected,
                const std::vector<core::EntityId>& selectedEntities) override;

    void drawPanel(core::ECS& ecs, core::EntityId selected, const std::vector<core::EntityId>& selectedEntities) override;

    // Kronos ("3D DCC Modeling Suite" -- real sub-object raycast picking):
    // what a viewport click resolves to. Reuses core::EditableMesh's own
    // SelectionMode enum (declared but previously unused anywhere) rather
    // than a second, duplicate mode enum.
    [[nodiscard]] core::EditableMesh::SelectionMode subObjectMode() const { return subObjectMode_; }
    void setSubObjectMode(core::EditableMesh::SelectionMode mode) { subObjectMode_ = mode; }

    // Real ray-vs-mesh picking, closing the exact gap this class's own
    // header comment used to document as out of scope ("building real
    // ray-vs-triangle/ray-vs-edge picking for sub-mesh elements is real,
    // separate scope"). Built on core::pickTriangleUv() (the same real
    // Moller-Trumbore triangle test MaterialPlugin's viewport click-to-
    // paint already proved end-to-end) to find the hit triangle, then
    // resolves that hit to the nearest real vertex/edge/face of that
    // triangle depending on subObjectMode(). `localOrigin`/`localDirection`
    // are already in component.mesh's own local space -- same division of
    // responsibility core::pickEntity()'s own AABB test uses; the caller
    // (ViewportPanel) transforms the world-space mouse ray through the
    // entity's inverse Transform matrix first. Returns true and updates
    // component's own selectedVertex/selectedEdge/selectedFace on a real
    // hit; a real, honest false (component left untouched) otherwise.
    bool pickSubObject(const core::EditableMeshComponent& component, glm::vec3 localOrigin, glm::vec3 localDirection,
                        float maxDistance) const;

    // Where the sub-object gizmo belongs, in component.mesh's own local
    // space, for whichever element subObjectMode() currently has selected
    // -- a real vertex position, an edge's real midpoint, or
    // EditableMesh::faceCentroid(), never an approximation. Returns the
    // origin if the current selection index is out of range (e.g. right
    // after a topology op shrank the mesh -- same "stale selection is a
    // real, honest no-op" precedent applyCsg() already established for
    // selectedFace/selectedEdge).
    [[nodiscard]] glm::vec3 subObjectAnchorLocal(const core::EditableMeshComponent& component) const;

    // Applies a real local-space translation to every real vertex the
    // current selection resolves to (1 vertex, the 2 vertices of an edge,
    // or the 3 vertices of a face) via EditableMesh::setVertexPosition(),
    // then re-uploads. Real, honest scope: this mesh's flat-shaded-per-
    // face storage gives each face its own private vertices even at a
    // shared corner (see createBox()'s own "24-vertex" comment) --
    // dragging one vertex/edge/face does NOT drag a position-coincident
    // vertex belonging to a neighboring face along with it, opening a
    // real seam, the same per-index (not per-position) scope bevelEdge()/
    // allEdges() already have. A future pass could add an explicit
    // "move all vertices at this position" toggle without changing
    // anything below.
    void translateSubObjectSelection(core::EditableMeshComponent& component, core::Renderable& renderable,
                                      glm::vec3 localDelta);

    // Kronos ("3D DCC Modeling Suite" -- true GPU sculpt brushes): a real
    // dispatch of core::ComputePbrPainter::sculpt() over EVERY real
    // vertex in `component.mesh`, brush-centered on the current
    // sub-object selection's own real anchor (subObjectAnchorLocal()),
    // then written back via EditableMesh::setVertexPosition() and
    // re-uploaded -- the exact same "real GPU round trip, CPU stays
    // authoritative" shape translateSubObjectSelection() already
    // establishes, just driven by a compute shader instead of a plain
    // add. Real, stated scope: this is a panel-button-driven stroke (one
    // real dispatch per "Apply Sculpt" click), not a live click-drag
    // gesture in the 3D viewport -- ViewportPanel's own interaction state
    // machine (free-fly camera, whole-entity gizmo, sub-object gizmo,
    // Ctrl+Click picking, drag-select-box) is already real and fairly
    // involved; wiring a 5th, continuous drag gesture into it is real,
    // separate scope deliberately not attempted here. `sculptBrushNormal`
    // is only meaningful for Clay Strips; `sculptDragDelta` only for
    // Grab; `sculptNeighborRadius` only for Smooth -- unused by the other
    // 3 modes, matching sculpt()'s own real, honest no-op convention for
    // its per-mode-only parameters.
    void applySculptStroke(core::EditableMeshComponent& component, core::Renderable& renderable);

    // Kronos (viewport error audit -- PBR paint brush ring): real,
    // current sculpt-brush radius (drawSculptSection()'s own "Radius"
    // slider), exposed so ViewportPanel can draw a real preview ring at
    // subObjectAnchorLocal() sized to what applySculptStroke() will
    // actually affect if clicked right now -- see that method's own
    // comment for why this is a static preview of the next stroke, not a
    // live mouse-drag brush (no continuous drag gesture exists here).
    [[nodiscard]] float sculptRadius() const { return sculptRadius_; }

private:
    // Kronos ("3D DCC Modeling Suite" -- real non-destructive modifier
    // stack): the panel section listing component.modifierStack's real
    // modifiers (enable/reorder/remove, and each one's own real
    // parameters), plus "Add <Type>" buttons -- see
    // core::ModifierStack.hpp's own class comment for why this is the
    // non-destructive alternative to applyCsg() below, not a replacement
    // for it.
    void drawModifierStackSection(core::EditableMeshComponent& component, core::Renderable& renderable);

    // Kronos ("3D DCC Modeling Suite" -- true GPU sculpt brushes): brush
    // mode/radius/strength controls plus the "Apply Sculpt" button that
    // calls applySculptStroke() -- see that method's own header comment
    // for the real, stated panel-button-vs-live-drag scope cut.
    void drawSculptSection(core::EditableMeshComponent& component, core::Renderable& renderable);

    // Rebuilds a real GPU core::Mesh from `component.mesh`'s current
    // vertices/indices and swaps it into `renderable.meshHandle` via
    // MeshLibrary::replaceMesh() -- the one real place a topology edit
    // actually becomes visible, called after every successful operation
    // below.
    void reuploadMesh(core::EditableMeshComponent& component, core::Renderable& renderable);

    // Real CSG panel action: combines `component.mesh` with a fresh box
    // (csgBoxHalfExtents_/csgBoxOffset_, via EditableMesh::createBox's
    // real `center` param) using core::booleanOp(), replaces
    // component.mesh with the result, resets the now-stale face/edge
    // selection (same "indices may have shifted" precedent Auto Unwrap's
    // own button already follows), and re-uploads. Guards against
    // booleanOp() returning an empty mesh (a real, observed BSP-CSG
    // outcome for a degenerate input, e.g. a zero-volume box) by leaving
    // component.mesh untouched and reporting the failure in
    // csgStatus_ instead of silently deleting the entity's geometry with
    // no undo.
    void applyCsg(core::EditableMeshComponent& component, core::Renderable& renderable, core::CsgOperation op);

    VmaAllocator allocator_;
    VkDevice device_;
    VkCommandPool cmdPool_;
    VkQueue queue_;
    core::MeshLibrary* meshLibrary_;

    // See subObjectMode()'s own comment. Face matches the pre-existing
    // Faces list being the first/most-used section in drawPanel() below.
    core::EditableMesh::SelectionMode subObjectMode_ = core::EditableMesh::SelectionMode::Face;

    float extrudeDistance_ = 0.5f;
    float insetAmount_ = 0.5f;
    float bevelAmount_ = 0.25f;
    float mergeThreshold_ = 0.01f;

    // Kronos ("3D DCC Modeling Suite" -- true GPU sculpt brushes): lazily
    // initialize()'d on first real "Apply Sculpt" click, same "no live
    // compute pipeline needed until actually used" reasoning
    // MaterialPlugin's own painter_ already establishes.
    core::ComputePbrPainter sculptPainter_;
    bool sculptPainterReady_ = false;
    core::SculptBrushMode sculptBrushMode_ = core::SculptBrushMode::Grab;
    float sculptRadius_ = 0.5f;
    float sculptStrength_ = 0.5f;
    glm::vec3 sculptDragDelta_{0.0f, 0.25f, 0.0f}; // Grab only
    float sculptNeighborRadius_ = 0.25f;           // Smooth only
    std::string sculptStatus_;

    // Kronos ("3D Model Maker" Phase 4 -- export/import).
    std::string exportPathBuffer_ = "exported_mesh";
    std::string importPathBuffer_ = "exported_mesh.kmesh";
    std::string exportImportStatus_;

    // Kronos ("Live Collaboration & In-Studio 3D Modeling Pipeline" --
    // Beta Roadmap, CSG). The second operand is always a fresh box --
    // EditableMesh::createBox() is the one real seed this plugin (and
    // core::ScriptMeshApi) knows how to build, same "Start Editing"
    // precedent above; picking an arbitrary second SCENE entity as the
    // other operand is real, separate scope (needs its own selection UI)
    // not attempted here.
    glm::vec3 csgBoxOffset_{1.0f, 0.0f, 0.0f};
    glm::vec3 csgBoxHalfExtents_{0.5f, 0.5f, 0.5f};
    std::string csgStatus_;

    // update()'s own per-entity "have I already uploaded this
    // editVersion" record -- a missing entry defaults to 0 via
    // unordered_map::operator[], which is exactly EditableMeshComponent::
    // editVersion's own default, so a freshly-created component (no
    // script edit yet) is correctly treated as "already up to date"
    // rather than triggering a redundant first-frame upload.
    std::unordered_map<core::EntityId, uint64_t> lastAppliedEditVersion_;
};

} // namespace engine::studio::plugins
