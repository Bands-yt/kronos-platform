#pragma once

#include <vector>

#include <volk.h>
#include <vk_mem_alloc.h>

#include "brokenbones/CliffGenerator.hpp"
#include "core/ECS.hpp"
#include "core/Mesh.hpp"
#include "core/Physics.hpp"
#include "core/ProceduralMaterials.hpp"

namespace engine::brokenbones {

struct GpuUpload {
    core::MeshLibrary* meshLibrary = nullptr;
    VmaAllocator allocator = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
};

// Spawns a CliffLayout as renderable, collidable entities. Mesh handles are
// recycled across rebuilds since MeshLibrary cannot unregister meshes; the
// caller must make sure the GPU is idle before rebuilding.
class CliffMap {
public:
    bool build(const CliffLayout& layout, core::ECS& ecs, core::Physics& physics, const GpuUpload& gpu,
               const core::ProceduralMaterialLibrary& materials);
    void clear(core::ECS& ecs, core::Physics& physics);

    [[nodiscard]] size_t entityCount() const { return entities_.size(); }

private:
    uint32_t uploadMesh(const GpuUpload& gpu, const std::vector<core::Vertex>& vertices,
                        const std::vector<uint32_t>& indices);

    std::vector<uint32_t> meshPool_;
    size_t nextMeshSlot_ = 0;
    std::vector<core::EntityId> entities_;
};

// 24-vertex box whose UVs are in metres * uvPerMetre, so large slabs tile
// their texture instead of stretching it.
void buildBoxMesh(glm::vec3 halfExtents, float uvPerMetre, std::vector<core::Vertex>& outVertices,
                  std::vector<uint32_t>& outIndices);

} // namespace engine::brokenbones
