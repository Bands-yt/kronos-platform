#include "tntwars/TntWarsUpgradeStation.hpp"

#include <string>

#include "core/Components.hpp"
#include "core/Interactable.hpp"

namespace engine::tntwars {

namespace {
core::EntityId spawnStationMarker(core::ECS& ecs, core::MeshLibrary& meshLibrary,
                                   const core::ProceduralMaterialLibrary& materials, VmaAllocator allocator,
                                   VkDevice device, VkCommandPool cmdPool, VkQueue queue, glm::vec3 position,
                                   UpgradeCategory category, const char* name) {
    glm::vec3 halfExtents(0.4f, 0.7f, 0.4f);
    core::Mesh mesh = core::Mesh::createBox(allocator, device, cmdPool, queue, halfExtents);
    if (mesh.vertexBuffer() == VK_NULL_HANDLE) return core::kNullEntity;
    uint32_t meshHandle = meshLibrary.registerMesh(std::move(mesh));

    core::EntityId entity = ecs.createEntity(name);
    if (auto* transform = ecs.tryGetComponent<core::Transform>(entity)) {
        transform->position = position + glm::vec3(0.0f, halfExtents.y, 0.0f);
    }

    // Real, distinct tint per category -- amber for Traversal (matches
    // this session's own "movement/speed" color language, see jump-pad/
    // zip-line prop tints elsewhere), cool cyan for Suit.
    glm::vec3 tint = category == UpgradeCategory::Traversal ? glm::vec3(0.95f, 0.6f, 0.2f) : glm::vec3(0.3f, 0.75f, 0.95f);

    auto& renderable = ecs.addComponent<core::Renderable>(entity);
    renderable.meshHandle = meshHandle;
    renderable.baseColor = glm::vec4(tint, 1.0f);
    renderable.metallic = 0.6f;
    renderable.roughness = 0.25f;
    renderable.albedoTexture = materials.metal.albedo;
    renderable.normalTexture = materials.metal.normal;
    renderable.metallicTexture = materials.metal.metallic;
    renderable.roughnessTexture = materials.metal.roughness;
    renderable.aoTexture = materials.metal.ao;
    renderable.emissiveColor = tint;
    renderable.emissiveIntensity = 0.45f;

    auto& meshSource = ecs.addComponent<core::MeshSource>(entity);
    meshSource.kind = core::MeshSourceKind::Box;
    meshSource.params = halfExtents;

    auto& interactable = ecs.addComponent<core::Interactable>(entity);
    interactable.prompt = category == UpgradeCategory::Traversal ? "Upgrade Traversal" : "Upgrade Suit";
    interactable.proximityEnabled = true;
    interactable.proximityRadius = 2.5f;

    ecs.addComponent<UpgradeStationLink>(entity, UpgradeStationLink{category});

    return entity;
}
} // namespace

std::vector<core::EntityId> spawnUpgradeStations(core::ECS& ecs, core::MeshLibrary& meshLibrary,
                                                    const core::ProceduralMaterialLibrary& materials,
                                                    VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool,
                                                    VkQueue queue, glm::vec3 basePosition, const char* locationLabel) {
    std::vector<core::EntityId> spawned;

    core::EntityId traversal =
        spawnStationMarker(ecs, meshLibrary, materials, allocator, device, cmdPool, queue,
                            basePosition + glm::vec3(-1.0f, 0.0f, 0.0f), UpgradeCategory::Traversal,
                            (std::string(locationLabel) + "_UpgradeTraversal").c_str());
    if (traversal != core::kNullEntity) spawned.push_back(traversal);

    core::EntityId suit =
        spawnStationMarker(ecs, meshLibrary, materials, allocator, device, cmdPool, queue,
                            basePosition + glm::vec3(1.0f, 0.0f, 0.0f), UpgradeCategory::Suit,
                            (std::string(locationLabel) + "_UpgradeSuit").c_str());
    if (suit != core::kNullEntity) spawned.push_back(suit);

    return spawned;
}

} // namespace engine::tntwars
