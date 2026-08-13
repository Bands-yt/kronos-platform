#include "tntwars/TntChargeVisual.hpp"

#include "core/Components.hpp"

namespace engine::tntwars {

core::EntityId spawnTntChargeVisual(core::ECS& ecs, core::MeshLibrary& meshLibrary,
                                      const core::ProceduralMaterialLibrary& materials, VmaAllocator allocator,
                                      VkDevice device, VkCommandPool cmdPool, VkQueue queue, glm::vec3 position,
                                      const char* name) {
    glm::vec3 halfExtents(0.18f, 0.18f, 0.18f);
    core::Mesh mesh = core::Mesh::createBox(allocator, device, cmdPool, queue, halfExtents);
    if (mesh.vertexBuffer() == VK_NULL_HANDLE) return core::kNullEntity;
    uint32_t meshHandle = meshLibrary.registerMesh(std::move(mesh));

    core::EntityId entity = ecs.createEntity(name);
    if (auto* transform = ecs.tryGetComponent<core::Transform>(entity)) {
        transform->position = position + glm::vec3(0.0f, halfExtents.y, 0.0f);
    }

    auto& renderable = ecs.addComponent<core::Renderable>(entity);
    renderable.meshHandle = meshHandle;
    renderable.baseColor = glm::vec4(0.85f, 0.1f, 0.05f, 1.0f);
    renderable.metallic = 0.1f;
    renderable.roughness = 0.5f;
    renderable.albedoTexture = materials.metal.albedo;
    renderable.normalTexture = materials.metal.normal;
    renderable.metallicTexture = materials.metal.metallic;
    renderable.roughnessTexture = materials.metal.roughness;
    renderable.aoTexture = materials.metal.ao;
    renderable.emissiveColor = glm::vec3(1.0f, 0.15f, 0.05f);
    renderable.emissiveIntensity = 0.8f; // real, deliberately bright -- reads as "live/armed" at a glance

    auto& meshSource = ecs.addComponent<core::MeshSource>(entity);
    meshSource.kind = core::MeshSourceKind::Box;
    meshSource.params = halfExtents;

    return entity;
}

} // namespace engine::tntwars
