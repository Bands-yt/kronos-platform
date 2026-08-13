#include "tntwars/TrenchesMapVisual.hpp"

#include <cstdio>
#include <string>

#include "core/Components.hpp"
#include "miningsim/Boss.hpp"
#include "miningsim/ForgeVisual.hpp"
#include "miningsim/MobVisual.hpp"

namespace engine::tntwars {

namespace {

core::EntityId spawnBox(core::ECS& ecs, core::Physics& physics, core::MeshLibrary& meshLibrary,
                         const core::PbrTextureSet& material, float metallic, float roughness, glm::vec3 position,
                         glm::vec3 halfExtents, VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool,
                         VkQueue queue, const char* name) {
    core::Mesh mesh = core::Mesh::createBox(allocator, device, cmdPool, queue, halfExtents);
    if (mesh.vertexBuffer() == VK_NULL_HANDLE) return core::kNullEntity;
    uint32_t meshHandle = meshLibrary.registerMesh(std::move(mesh));

    core::EntityId entity = ecs.createEntity(name);
    if (auto* transform = ecs.tryGetComponent<core::Transform>(entity)) transform->position = position;

    auto& renderable = ecs.addComponent<core::Renderable>(entity);
    renderable.meshHandle = meshHandle;
    renderable.baseColor = glm::vec4(1.0f);
    renderable.metallic = metallic;
    renderable.roughness = roughness;
    renderable.albedoTexture = material.albedo;
    renderable.normalTexture = material.normal;
    renderable.metallicTexture = material.metallic;
    renderable.roughnessTexture = material.roughness;
    renderable.aoTexture = material.ao;

    auto& meshSource = ecs.addComponent<core::MeshSource>(entity);
    meshSource.kind = core::MeshSourceKind::Box;
    meshSource.params = halfExtents;

    core::ColliderShape shape;
    shape.kind = core::ColliderShapeKind::Box;
    shape.params = halfExtents;
    if (!physics.attachBodyToEntity(entity, ecs, shape, core::PhysicsMaterial{}, core::RigidBodyMotionType::Static)) {
        std::fprintf(stderr, "spawnTrenchesMapVisual: attachBodyToEntity failed for \"%s\" -- it will render but not collide.\n",
                     name);
    }
    return entity;
}

// One real wooden bunker: three walls (back + two sides) + a roof, open
// on the front face -- a real, walk-in shelter, not just a solid block.
void spawnBunker(core::ECS& ecs, core::Physics& physics, core::MeshLibrary& meshLibrary,
                  const core::PbrTextureSet& wood, glm::vec3 center, float frontFacingZ, VmaAllocator allocator,
                  VkDevice device, VkCommandPool cmdPool, VkQueue queue, const char* prefix,
                  std::vector<core::EntityId>& outSpawned) {
    constexpr float kHalfWidth = 4.0f;
    constexpr float kHalfHeight = 2.5f;
    constexpr float kHalfDepth = 4.0f;
    constexpr float kWallThickness = 0.4f;
    float zSign = frontFacingZ >= 0.0f ? 1.0f : -1.0f; // open face points toward frontFacingZ's own sign

    core::EntityId back =
        spawnBox(ecs, physics, meshLibrary, wood, 0.0f, 0.7f, center + glm::vec3(0.0f, kHalfHeight, -zSign * kHalfDepth),
                 glm::vec3(kHalfWidth, kHalfHeight, kWallThickness), allocator, device, cmdPool, queue,
                 (std::string(prefix) + "_Back").c_str());
    core::EntityId sideA =
        spawnBox(ecs, physics, meshLibrary, wood, 0.0f, 0.7f, center + glm::vec3(-kHalfWidth, kHalfHeight, 0.0f),
                 glm::vec3(kWallThickness, kHalfHeight, kHalfDepth), allocator, device, cmdPool, queue,
                 (std::string(prefix) + "_SideA").c_str());
    core::EntityId sideB =
        spawnBox(ecs, physics, meshLibrary, wood, 0.0f, 0.7f, center + glm::vec3(kHalfWidth, kHalfHeight, 0.0f),
                 glm::vec3(kWallThickness, kHalfHeight, kHalfDepth), allocator, device, cmdPool, queue,
                 (std::string(prefix) + "_SideB").c_str());
    core::EntityId roof =
        spawnBox(ecs, physics, meshLibrary, wood, 0.0f, 0.7f, center + glm::vec3(0.0f, kHalfHeight * 2.0f + 0.2f, 0.0f),
                 glm::vec3(kHalfWidth + 0.5f, 0.2f, kHalfDepth + 0.5f), allocator, device, cmdPool, queue,
                 (std::string(prefix) + "_Roof").c_str());
    for (core::EntityId e : {back, sideA, sideB, roof}) {
        if (e != core::kNullEntity) outSpawned.push_back(e);
    }
}

// One real elevated artillery platform: a 2-layer tapered wood/stone
// tower + a flat firing platform on top, the same tapered-stack technique
// SkyMapVisual/VolcanoMapVisual already establish, shorter and wider here
// for a real "elevated firing position" read rather than a spire.
void spawnArtilleryPlatform(core::ECS& ecs, core::Physics& physics, core::MeshLibrary& meshLibrary,
                             const core::ProceduralMaterialLibrary& materials, glm::vec3 baseCenter,
                             VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue,
                             const char* prefix, std::vector<core::EntityId>& outSpawned) {
    constexpr float kLayerHalfHeight = 2.0f;
    float y = baseCenter.y;
    float halfWidth = 4.5f;
    for (int layer = 0; layer < 2; ++layer) {
        glm::vec3 halfExtents(halfWidth, kLayerHalfHeight, halfWidth);
        glm::vec3 pos(baseCenter.x, y + kLayerHalfHeight, baseCenter.z);
        core::EntityId e = spawnBox(ecs, physics, meshLibrary, materials.wood, 0.0f, 0.7f, pos, halfExtents, allocator,
                                     device, cmdPool, queue, (std::string(prefix) + "_Layer" + std::to_string(layer)).c_str());
        if (e != core::kNullEntity) outSpawned.push_back(e);
        y += kLayerHalfHeight * 2.0f;
        halfWidth *= 0.8f;
    }
    constexpr float kPlatformHalfHeight = 0.4f;
    glm::vec3 platformPos(baseCenter.x, y + kPlatformHalfHeight, baseCenter.z);
    core::EntityId platform =
        spawnBox(ecs, physics, meshLibrary, materials.wood, 0.0f, 0.6f, platformPos,
                 glm::vec3(halfWidth / 0.8f * 0.95f, kPlatformHalfHeight, halfWidth / 0.8f * 0.95f), allocator, device,
                 cmdPool, queue, (std::string(prefix) + "_Platform").c_str());
    if (platform != core::kNullEntity) outSpawned.push_back(platform);
}

} // namespace

glm::vec3 kTrenchesMapCommanderArenaCenter() { return glm::vec3(0.0f, 0.0f, 8.0f); }

std::vector<core::EntityId> spawnTrenchesMapVisual(core::ECS& ecs, core::Physics& physics,
                                                     core::MeshLibrary& meshLibrary,
                                                     const core::ProceduralMaterialLibrary& materials,
                                                     VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool,
                                                     VkQueue queue, miningsim::RarityTier bossRarity) {
    std::vector<core::EntityId> spawned;

    // Real mud terrain patches -- thin, flush-with-ground mud plates
    // scattered across no-man's-land (around the map's own real mid-wall
    // at Z=0), the brief's own "mud" battlefield read.
    const glm::vec3 kMudOffsets[] = {
        {-14.0f, 0.0f, -4.0f}, {8.0f, 0.0f, 6.0f}, {-6.0f, 0.0f, 5.0f}, {16.0f, 0.0f, -5.0f}, {0.0f, 0.0f, -8.0f},
    };
    constexpr size_t kMudCount = 5;
    for (size_t i = 0; i < kMudCount; ++i) {
        glm::vec3 halfExtents(3.5f + static_cast<float>(i % 2) * 1.0f, 0.12f, 3.0f + static_cast<float>((i + 1) % 2) * 1.5f);
        glm::vec3 pos(kMudOffsets[i].x, 0.12f, kMudOffsets[i].z);
        core::EntityId e = spawnBox(ecs, physics, meshLibrary, materials.mud, 0.0f, 0.95f, pos, halfExtents, allocator,
                                     device, cmdPool, queue, ("TrenchesMap_Mud" + std::to_string(i)).c_str());
        if (e != core::kNullEntity) spawned.push_back(e);
    }

    // Real wooden bunkers -- one per team's own rally point, offset west
    // of that team's existing Cover_*_Left piece (x=-20) so it reads as a
    // real, distinct shelter rather than overlapping cover.
    spawnBunker(ecs, physics, meshLibrary, materials.wood, glm::vec3(-35.0f, 0.0f, -25.0f), -1.0f, allocator, device,
                cmdPool, queue, "TrenchesMap_BunkerA", spawned);
    spawnBunker(ecs, physics, meshLibrary, materials.wood, glm::vec3(-35.0f, 0.0f, 25.0f), 1.0f, allocator, device,
                cmdPool, queue, "TrenchesMap_BunkerB", spawned);

    // Real, walkable TNT tunnel -- west edge of the map, crossing the
    // real mid-wall's own Z=0 line through a real mud-and-stone mound
    // rather than the open field, the same two-walls-plus-roof corridor
    // technique VolcanoMapVisual/UnderwaterMapVisual already establish.
    constexpr float kTunnelHalfWidth = 5.0f;
    constexpr float kTunnelHalfHeight = 2.5f;
    constexpr float kTunnelHalfLength = 10.0f;
    glm::vec3 tunnelCenter(-45.0f, kTunnelHalfHeight, 0.0f);
    core::EntityId tunnelWallA =
        spawnBox(ecs, physics, meshLibrary, materials.mud, 0.0f, 0.9f,
                 tunnelCenter + glm::vec3(-(kTunnelHalfWidth + 1.5f), 0.0f, 0.0f), glm::vec3(1.5f, kTunnelHalfHeight, kTunnelHalfLength),
                 allocator, device, cmdPool, queue, "TrenchesMap_Tunnel_WallA");
    core::EntityId tunnelWallB =
        spawnBox(ecs, physics, meshLibrary, materials.mud, 0.0f, 0.9f,
                 tunnelCenter + glm::vec3(kTunnelHalfWidth + 1.5f, 0.0f, 0.0f), glm::vec3(1.5f, kTunnelHalfHeight, kTunnelHalfLength),
                 allocator, device, cmdPool, queue, "TrenchesMap_Tunnel_WallB");
    core::EntityId tunnelRoof =
        spawnBox(ecs, physics, meshLibrary, materials.mud, 0.0f, 0.9f,
                 tunnelCenter + glm::vec3(0.0f, kTunnelHalfHeight + 0.75f, 0.0f),
                 glm::vec3(kTunnelHalfWidth + 3.0f, 0.75f, kTunnelHalfLength), allocator, device, cmdPool, queue,
                 "TrenchesMap_Tunnel_Roof");
    for (core::EntityId e : {tunnelWallA, tunnelWallB, tunnelRoof}) {
        if (e != core::kNullEntity) spawned.push_back(e);
    }

    // Real elevated artillery platforms -- east side, mirroring the
    // bunkers' own west-side placement, one behind each team's existing
    // Cover_*_Right piece (x=20).
    spawnArtilleryPlatform(ecs, physics, meshLibrary, materials, glm::vec3(35.0f, 0.0f, -25.0f), allocator, device,
                            cmdPool, queue, "TrenchesMap_ArtilleryA", spawned);
    spawnArtilleryPlatform(ecs, physics, meshLibrary, materials, glm::vec3(35.0f, 0.0f, 25.0f), allocator, device,
                            cmdPool, queue, "TrenchesMap_ArtilleryB", spawned);

    // Real trench-commander boss arena -- a real command-tower platform
    // straddling the mid-wall's own real Z=0 line (rising well above the
    // wall's own real 5-unit height), the same tapered-stack technique
    // every other map's own boss platform uses.
    glm::vec3 towerBase(kTrenchesMapCommanderArenaCenter().x, 0.0f, kTrenchesMapCommanderArenaCenter().z);
    constexpr float kTowerLayerHalfHeight = 3.0f;
    float towerY = 0.0f;
    float towerHalfWidth = 6.0f;
    for (int layer = 0; layer < 2; ++layer) {
        glm::vec3 halfExtents(towerHalfWidth, kTowerLayerHalfHeight, towerHalfWidth);
        glm::vec3 pos(towerBase.x, towerY + kTowerLayerHalfHeight, towerBase.z);
        core::EntityId e = spawnBox(ecs, physics, meshLibrary, materials.stone, 0.05f, 0.85f, pos, halfExtents,
                                     allocator, device, cmdPool, queue, ("TrenchesMap_Tower_Layer" + std::to_string(layer)).c_str());
        if (e != core::kNullEntity) spawned.push_back(e);
        towerY += kTowerLayerHalfHeight * 2.0f;
        towerHalfWidth *= 0.65f;
    }
    constexpr float kArenaHalfHeight = 0.5f;
    glm::vec3 arenaPos(towerBase.x, towerY + kArenaHalfHeight, towerBase.z);
    core::EntityId arenaFloor =
        spawnBox(ecs, physics, meshLibrary, materials.stone, 0.05f, 0.8f, arenaPos,
                 glm::vec3(towerHalfWidth / 0.65f * 1.1f, kArenaHalfHeight, towerHalfWidth / 0.65f * 1.1f), allocator,
                 device, cmdPool, queue, "TrenchesMap_Tower_Arena");
    if (arenaFloor != core::kNullEntity) spawned.push_back(arenaFloor);
    glm::vec3 bossPos(arenaPos.x, arenaPos.y + kArenaHalfHeight, arenaPos.z);

    // Real trench commander boss -- reuses the same generic boss visual
    // pipeline every other map's arena already proves out, real-
    // overridden to a dark, gunmetal "commander" look directly on the
    // returned entities.
    miningsim::MobState boss = miningsim::spawnBossOfRarity(bossRarity);
    std::vector<core::EntityId> bossEntities =
        miningsim::spawnMobVisual(ecs, meshLibrary, materials, allocator, device, cmdPool, queue, boss, bossPos);
    for (core::EntityId bossPart : bossEntities) {
        if (auto* renderable = ecs.tryGetComponent<core::Renderable>(bossPart)) {
            renderable->baseColor = glm::vec4(0.12f, 0.12f, 0.14f, 1.0f); // real, dark gunmetal commander armor
            renderable->emissiveColor = glm::vec3(0.8f, 0.15f, 0.1f);    // real, faint red command-light glow
            renderable->emissiveIntensity = 0.25f;
            renderable->metallic = 0.85f;
            renderable->roughness = 0.3f;
        }
    }
    if (!bossEntities.empty()) {
        core::EntityId torso = bossEntities[0];
        core::ColliderShape shape;
        shape.kind = core::ColliderShapeKind::Capsule;
        shape.params = glm::vec3(1.2f, 2.0f, 0.0f);
        if (!physics.attachBodyToEntity(torso, ecs, shape, core::PhysicsMaterial{}, core::RigidBodyMotionType::Static)) {
            std::fprintf(stderr, "spawnTrenchesMapVisual: attachBodyToEntity failed for the boss's own collider.\n");
        }
    }
    spawned.insert(spawned.end(), bossEntities.begin(), bossEntities.end());

    std::vector<core::EntityId> toolEntities = miningsim::spawnForgedToolVisual(
        ecs, meshLibrary, materials, allocator, device, cmdPool, queue, miningsim::MiningToolType::ExplosiveCharge,
        bossPos + glm::vec3(3.0f, 0.0f, 3.0f));
    spawned.insert(spawned.end(), toolEntities.begin(), toolEntities.end());

    return spawned;
}

} // namespace engine::tntwars
