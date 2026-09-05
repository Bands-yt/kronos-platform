#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include <volk.h>
#include <vk_mem_alloc.h>

#include <glm/glm.hpp>

#include "core/Components.hpp"
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

private:
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

    float extrudeDistance_ = 0.5f;
    float insetAmount_ = 0.5f;
    float bevelAmount_ = 0.25f;
    float mergeThreshold_ = 0.01f;

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
