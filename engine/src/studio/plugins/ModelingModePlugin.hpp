#pragma once

#include <string>
#include <vector>

#include <volk.h>
#include <vk_mem_alloc.h>

#include "core/Components.hpp"
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
class ModelingModePlugin final : public IStudioPlugin {
public:
    ModelingModePlugin(VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue,
                        core::MeshLibrary& meshLibrary);

    [[nodiscard]] const char* name() const override { return "Modeling Mode"; }
    [[nodiscard]] const char* category() const override { return "World"; }

    void drawPanel(core::ECS& ecs, core::EntityId selected, const std::vector<core::EntityId>& selectedEntities) override;

private:
    // Rebuilds a real GPU core::Mesh from `component.mesh`'s current
    // vertices/indices and swaps it into `renderable.meshHandle` via
    // MeshLibrary::replaceMesh() -- the one real place a topology edit
    // actually becomes visible, called after every successful operation
    // below.
    void reuploadMesh(core::EditableMeshComponent& component, core::Renderable& renderable);

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
};

} // namespace engine::studio::plugins
