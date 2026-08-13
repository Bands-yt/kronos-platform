#include "miningsim/DungeonVisual.hpp"

#include "core/Components.hpp"
#include "miningsim/Rarity.hpp"

namespace engine::miningsim {

namespace {

core::EntityId spawnBox(core::ECS& ecs, core::MeshLibrary& meshLibrary, const char* name, glm::vec3 position,
                         glm::vec3 halfExtents, const core::PbrTextureSet& material, glm::vec3 emissiveColor,
                         float emissiveIntensity, VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool,
                         VkQueue queue) {
    core::Mesh mesh = core::Mesh::createBox(allocator, device, cmdPool, queue, halfExtents);
    if (mesh.vertexBuffer() == VK_NULL_HANDLE) return core::kNullEntity;
    uint32_t meshHandle = meshLibrary.registerMesh(std::move(mesh));

    core::EntityId entity = ecs.createEntity(name);
    if (auto* transform = ecs.tryGetComponent<core::Transform>(entity)) transform->position = position;

    auto& renderable = ecs.addComponent<core::Renderable>(entity);
    renderable.meshHandle = meshHandle;
    renderable.baseColor = glm::vec4(1.0f);
    renderable.metallic = 0.05f;
    renderable.roughness = 0.85f;
    renderable.albedoTexture = material.albedo;
    renderable.normalTexture = material.normal;
    renderable.metallicTexture = material.metallic;
    renderable.roughnessTexture = material.roughness;
    renderable.aoTexture = material.ao;
    renderable.emissiveColor = emissiveColor;
    renderable.emissiveIntensity = emissiveIntensity;

    auto& meshSource = ecs.addComponent<core::MeshSource>(entity);
    meshSource.kind = core::MeshSourceKind::Box;
    meshSource.params = halfExtents;
    return entity;
}

} // namespace

std::vector<core::EntityId> spawnDungeonVisual(core::ECS& ecs, core::MeshLibrary& meshLibrary,
                                                const core::ProceduralMaterialLibrary& materials,
                                                VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool,
                                                VkQueue queue, const DungeonLayout& layout, glm::vec3 origin,
                                                float cellSize) {
    std::vector<core::EntityId> spawned;
    glm::vec3 beaconColor = rarityTierColor(layout.rarity);

    auto cellToWorld = [&](glm::ivec2 cell) {
        return origin + glm::vec3(static_cast<float>(cell.x) * cellSize, 0.0f, static_cast<float>(cell.y) * cellSize);
    };

    for (size_t i = 0; i < layout.rooms.size(); ++i) {
        const DungeonRoom& room = layout.rooms[i];
        glm::vec2 roomCenterCells(static_cast<float>(room.gridPosition.x) + static_cast<float>(room.gridSize.x) * 0.5f,
                                   static_cast<float>(room.gridPosition.y) + static_cast<float>(room.gridSize.y) * 0.5f);
        glm::vec3 floorHalfExtents(static_cast<float>(room.gridSize.x) * cellSize * 0.5f, kDungeonRoomFloorHalfHeight,
                                    static_cast<float>(room.gridSize.y) * cellSize * 0.5f);
        glm::vec3 floorPosition = origin + glm::vec3(roomCenterCells.x * cellSize, kDungeonRoomFloorHalfHeight,
                                                       roomCenterCells.y * cellSize);
        core::EntityId floor = spawnBox(ecs, meshLibrary, "DungeonRoom_Floor", floorPosition, floorHalfExtents,
                                         materials.stone, glm::vec3(0.0f), 0.0f, allocator, device, cmdPool, queue);
        if (floor != core::kNullEntity) spawned.push_back(floor);

        glm::vec3 beaconPosition = floorPosition + glm::vec3(0.0f, kDungeonRoomFloorHalfHeight + kDungeonBeaconHalfHeight, 0.0f);
        core::EntityId beacon =
            spawnBox(ecs, meshLibrary, "DungeonRoom_Beacon", beaconPosition, glm::vec3(0.3f, kDungeonBeaconHalfHeight, 0.3f),
                     materials.crystal, beaconColor, 1.2f, allocator, device, cmdPool, queue);
        if (beacon != core::kNullEntity) spawned.push_back(beacon);
    }

    for (const DungeonCorridor& corridor : layout.corridors) {
        glm::vec3 fromWorld = cellToWorld(corridor.from);
        glm::vec3 toWorld = cellToWorld(corridor.to);
        glm::vec3 mid = (fromWorld + toWorld) * 0.5f;
        glm::vec3 delta = toWorld - fromWorld;
        float length = glm::length(delta);
        if (length < 0.01f) continue;
        // Real, axis-aligned corridor strip -- long axis picked from
        // whichever of X/Z the delta is larger along (every real corridor
        // in this grid-based layout runs axis-aligned since rooms connect
        // center-to-center on an integer grid), matching this module's
        // own "simple, bounded technique" scope.
        glm::vec3 halfExtents = std::abs(delta.x) >= std::abs(delta.z)
                                     ? glm::vec3(length * 0.5f, kDungeonRoomFloorHalfHeight, kDungeonCorridorHalfWidth)
                                     : glm::vec3(kDungeonCorridorHalfWidth, kDungeonRoomFloorHalfHeight, length * 0.5f);
        core::EntityId strip =
            spawnBox(ecs, meshLibrary, "DungeonCorridor", glm::vec3(mid.x, kDungeonRoomFloorHalfHeight, mid.z), halfExtents,
                     materials.stone, glm::vec3(0.0f), 0.0f, allocator, device, cmdPool, queue);
        if (strip != core::kNullEntity) spawned.push_back(strip);
    }

    return spawned;
}

} // namespace engine::miningsim
