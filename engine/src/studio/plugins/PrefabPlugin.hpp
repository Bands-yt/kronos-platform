#pragma once

#include <glm/glm.hpp>
#include <volk.h>
#include <vk_mem_alloc.h>

#include "core/Mesh.hpp"
#include "core/Prefab.hpp"
#include "studio/IStudioPlugin.hpp"

namespace engine::studio::plugins {

// Authors, saves/loads, and spawns core::Prefab templates. Prefabs are
// authored directly here (shape kind + params + material via sliders),
// not captured from an arbitrary existing scene entity -- meshes don't
// carry provenance (see Prefab.hpp's header comment), so "grab this
// entity's exact mesh as a template" isn't supported; building the
// template's shape here instead sidesteps that entirely.
//
// Each library entry caches one real GPU mesh handle, built once and
// reused across every spawned instance -- not rebuilt per spawn. Editing
// a prefab's shape after spawning instances and clicking "Rebuild Mesh"
// updates that shared handle in place (via MeshLibrary::replaceMesh,
// same mechanism core::Terrain's chunk regeneration uses), which means
// every already-spawned instance of that prefab visually updates too --
// a deliberate "live prefab" behavior, not a bug.
class PrefabPlugin final : public IStudioPlugin {
public:
    PrefabPlugin(VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue,
                 core::MeshLibrary& meshLibrary);

    [[nodiscard]] const char* name() const override { return "Prefabs"; }
    [[nodiscard]] const char* category() const override { return "World"; }

    void drawPanel(core::ECS& ecs, core::EntityId selected, const std::vector<core::EntityId>& selectedEntities) override;

private:
    struct LoadedPrefab {
        core::Prefab data;
        uint32_t meshHandle = core::Renderable::kInvalidHandle; // kInvalidHandle until "Build Mesh" is clicked
    };

    [[nodiscard]] core::Mesh buildMesh(const core::Prefab& prefab) const;
    void spawnInstance(const LoadedPrefab& entry, glm::vec3 position, core::ECS& ecs) const;

    VmaAllocator allocator_;
    VkDevice device_;
    VkCommandPool cmdPool_;
    VkQueue queue_;
    core::MeshLibrary* meshLibrary_;

    std::vector<LoadedPrefab> library_; // authored/loaded prefabs available this session
    int activeIndex_ = -1;
    char pathBuffer_[256] = "prefabs/prop.prefab";
    glm::vec3 spawnPosition_{0.0f, 1.0f, 0.0f};
};

} // namespace engine::studio::plugins
