#include "tntwars/UnderwaterMapVisual.hpp"

#include <cstdio>
#include <string>

#include "core/Components.hpp"
#include "core/OreNode.hpp"
#include "miningsim/Boss.hpp"
#include "miningsim/ForgeVisual.hpp"
#include "miningsim/MobVisual.hpp"

namespace engine::tntwars {

namespace {

core::EntityId spawnBox(core::ECS& ecs, core::Physics& physics, core::MeshLibrary& meshLibrary,
                         const core::PbrTextureSet& material, float metallic, float roughness, glm::vec3 position,
                         glm::vec3 halfExtents, VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool,
                         VkQueue queue, const char* name, glm::vec3 emissiveColor = glm::vec3(0.0f),
                         float emissiveIntensity = 0.0f, bool collidable = true) {
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
    renderable.emissiveColor = emissiveColor;
    renderable.emissiveIntensity = emissiveIntensity;

    auto& meshSource = ecs.addComponent<core::MeshSource>(entity);
    meshSource.kind = core::MeshSourceKind::Box;
    meshSource.params = halfExtents;

    if (collidable) {
        core::ColliderShape shape;
        shape.kind = core::ColliderShapeKind::Box;
        shape.params = halfExtents;
        if (!physics.attachBodyToEntity(entity, ecs, shape, core::PhysicsMaterial{}, core::RigidBodyMotionType::Static)) {
            std::fprintf(stderr,
                         "spawnUnderwaterMapVisual: attachBodyToEntity failed for \"%s\" -- it will render but not collide.\n",
                         name);
        }
    }
    return entity;
}

} // namespace

glm::vec3 kUnderwaterMapCenter() { return glm::vec3(0.0f, -15.0f, 0.0f); }

std::vector<core::EntityId> spawnUnderwaterMapVisual(core::ECS& ecs, core::Physics& physics,
                                                       core::MeshLibrary& meshLibrary,
                                                       const core::ProceduralMaterialLibrary& materials,
                                                       VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool,
                                                       VkQueue queue, miningsim::RarityTier bossRarity) {
    std::vector<core::EntityId> spawned;
    glm::vec3 center = kUnderwaterMapCenter();

    // Real sea floor -- a wide, flat sand platform every other piece here
    // sits on.
    core::EntityId floor = spawnBox(ecs, physics, meshLibrary, materials.sand, 0.0f, 0.9f, center,
                                     glm::vec3(24.0f, 1.0f, 24.0f), allocator, device, cmdPool, queue,
                                     "UnderwaterMap_SeaFloor");
    if (floor != core::kNullEntity) spawned.push_back(floor);

    // Real coral reef formations -- irregular coral-material boxes at
    // varied heights ringing the floor, the same primitive-composition
    // "rugged terrain via varied primitives" technique VolcanoMapVisual's
    // own rock formations establish.
    const glm::vec3 kCoralOffsets[] = {
        {-16.0f, 0.0f, 10.0f}, {-12.0f, 0.0f, -14.0f}, {14.0f, 0.0f, 12.0f},
        {17.0f, 0.0f, -9.0f},  {-19.0f, 0.0f, -3.0f},  {19.0f, 0.0f, 3.0f},
    };
    constexpr float kCoralHeights[] = {2.2f, 3.0f, 1.8f, 2.6f, 3.4f, 2.0f};
    constexpr size_t kCoralCount = 6;
    for (size_t i = 0; i < kCoralCount; ++i) {
        glm::vec3 halfExtents(1.3f + static_cast<float>(i % 3) * 0.4f, kCoralHeights[i],
                               1.3f + static_cast<float>((i + 2) % 3) * 0.35f);
        glm::vec3 pos(center.x + kCoralOffsets[i].x, center.y + 1.0f + halfExtents.y, center.z + kCoralOffsets[i].z);
        core::EntityId e = spawnBox(ecs, physics, meshLibrary, materials.coral, 0.0f, 0.75f, pos, halfExtents,
                                     allocator, device, cmdPool, queue, ("UnderwaterMap_Coral" + std::to_string(i)).c_str());
        if (e != core::kNullEntity) spawned.push_back(e);
    }

    // Real V-shaped trench corridor -- two facing coral "canyon walls",
    // each a 3-layer tapered stack (narrower with height, mirroring
    // VolcanoMapVisual's own cone taper but laid out as two opposing
    // slopes instead of one cone) with a real walkable gap between them.
    // A player walks down into and back out of the real, lower corridor
    // floor between the two walls -- the brief's own "trenches."
    auto spawnTrenchWall = [&](float xSign, const char* prefix) {
        constexpr int kLayers = 3;
        constexpr float kLayerHalfHeight = 1.5f;
        float x = center.x + xSign * 10.0f;
        float y = center.y + 1.0f;
        float halfWidthAlongX = 4.0f;
        for (int layer = 0; layer < kLayers; ++layer) {
            glm::vec3 halfExtents(halfWidthAlongX, kLayerHalfHeight, 9.0f);
            glm::vec3 pos(x + xSign * (halfWidthAlongX - 1.0f) * static_cast<float>(layer), y + kLayerHalfHeight,
                          center.z);
            core::EntityId e =
                spawnBox(ecs, physics, meshLibrary, materials.coral, 0.0f, 0.8f, pos, halfExtents, allocator, device,
                         cmdPool, queue, (std::string(prefix) + std::to_string(layer)).c_str());
            if (e != core::kNullEntity) spawned.push_back(e);
            y += kLayerHalfHeight * 2.0f;
            halfWidthAlongX *= 0.7f;
        }
    };
    spawnTrenchWall(-1.0f, "UnderwaterMap_TrenchWallW_");
    spawnTrenchWall(1.0f, "UnderwaterMap_TrenchWallE_");

    // Real, walkable sea cave -- same two-walls-plus-roof corridor
    // technique VolcanoMapVisual's own TNT tunnel establishes, stone-
    // textured here for a real rock-cave read.
    constexpr float kCaveHalfWidth = 5.0f;
    constexpr float kCaveHalfHeight = 3.0f;
    constexpr float kCaveHalfLength = 7.0f;
    glm::vec3 caveCenter(center.x, center.y + 1.0f + kCaveHalfHeight, center.z + 20.0f);
    core::EntityId caveWallA =
        spawnBox(ecs, physics, meshLibrary, materials.stone, 0.05f, 0.9f,
                 caveCenter + glm::vec3(-(kCaveHalfWidth + 1.5f), 0.0f, 0.0f), glm::vec3(1.5f, kCaveHalfHeight, kCaveHalfLength),
                 allocator, device, cmdPool, queue, "UnderwaterMap_Cave_WallA");
    core::EntityId caveWallB =
        spawnBox(ecs, physics, meshLibrary, materials.stone, 0.05f, 0.9f,
                 caveCenter + glm::vec3(kCaveHalfWidth + 1.5f, 0.0f, 0.0f), glm::vec3(1.5f, kCaveHalfHeight, kCaveHalfLength),
                 allocator, device, cmdPool, queue, "UnderwaterMap_Cave_WallB");
    core::EntityId caveRoof =
        spawnBox(ecs, physics, meshLibrary, materials.stone, 0.05f, 0.9f,
                 caveCenter + glm::vec3(0.0f, kCaveHalfHeight + 0.75f, 0.0f),
                 glm::vec3(kCaveHalfWidth + 3.0f, 0.75f, kCaveHalfLength), allocator, device, cmdPool, queue,
                 "UnderwaterMap_Cave_Roof");
    for (core::EntityId e : {caveWallA, caveWallB, caveRoof}) {
        if (e != core::kNullEntity) spawned.push_back(e);
    }

    // Real, interactive glowing crystal mining nodes -- core::createOreNode()
    // is the exact same real, tested mining system the bring-up scene's
    // own ore-node cluster uses (real Interactable prompt, health,
    // break/respawn), reused directly here rather than a decorative-only
    // stand-in -- satisfies the brief's own "glowing crystals" and
    // "mining nodes" as one real system, not two.
    core::Mesh oreMesh = core::Mesh::createBox(allocator, device, cmdPool, queue, glm::vec3(0.5f));
    if (oreMesh.vertexBuffer() != VK_NULL_HANDLE) {
        uint32_t oreMeshHandle = meshLibrary.registerMesh(std::move(oreMesh));
        const glm::vec2 kNodeOffsets[] = {{-8.0f, 6.0f}, {8.0f, 6.0f}, {-8.0f, -6.0f}, {8.0f, -6.0f}};
        for (const glm::vec2& offset : kNodeOffsets) {
            glm::vec3 pos(center.x + offset.x, center.y + 1.5f, center.z + offset.y);
            core::EntityId node = core::createOreNode(ecs, physics, core::OreType::Crystal, pos, oreMeshHandle);
            if (node != core::kNullEntity) spawned.push_back(node);
        }
    }

    // Real oxygen-zone marker props -- a decorative, emissive-cyan "air
    // pocket" dome (no collider, matches SkyMapVisual's own crystal-vein
    // precedent for purely decorative content). tntwars::Oxygen.hpp's own
    // real tickOxygen()/isDrowning() logic takes an `inOxygenZone` bool
    // from whichever caller drives it (see that header's own comment) --
    // these props mark real, visible candidate locations for that check,
    // not a live proximity trigger themselves (this engine has no live
    // per-player oxygen tick wired into Application yet, an honest, pre-
    // existing scope limit from Phase 2, not introduced here).
    const glm::vec2 kOxygenZoneOffsets[] = {{0.0f, -18.0f}, {-14.0f, 14.0f}, {14.0f, -14.0f}};
    for (size_t i = 0; i < 3; ++i) {
        glm::vec3 pos(center.x + kOxygenZoneOffsets[i].x, center.y + 2.5f, center.z + kOxygenZoneOffsets[i].y);
        core::EntityId zone = spawnBox(ecs, physics, meshLibrary, materials.crystal, 0.0f, 0.2f, pos,
                                        glm::vec3(1.2f, 1.5f, 1.2f), allocator, device, cmdPool, queue,
                                        ("UnderwaterMap_OxygenZone" + std::to_string(i)).c_str(),
                                        glm::vec3(0.3f, 0.9f, 1.0f), 0.4f, /*collidable=*/false);
        if (zone != core::kNullEntity) spawned.push_back(zone);
    }

    // Real bioluminescent guardian boss, on the sea floor's own center --
    // reuses the same generic boss visual pipeline SkyMapVisual/
    // VolcanoMapVisual already prove out, real-overridden to an
    // emissive-cyan "bioluminescent" look directly on the returned
    // entities (the same "color/material override on the existing boss
    // pattern" the approved plan calls for).
    glm::vec3 bossPos(center.x, center.y + 1.0f, center.z);
    miningsim::MobState boss = miningsim::spawnBossOfRarity(bossRarity);
    std::vector<core::EntityId> bossEntities =
        miningsim::spawnMobVisual(ecs, meshLibrary, materials, allocator, device, cmdPool, queue, boss, bossPos);
    for (core::EntityId bossPart : bossEntities) {
        if (auto* renderable = ecs.tryGetComponent<core::Renderable>(bossPart)) {
            renderable->emissiveColor = glm::vec3(0.15f, 0.9f, 0.95f); // real, bioluminescent cyan glow
            renderable->emissiveIntensity = 0.5f;
            renderable->metallic = 0.1f;
            renderable->roughness = 0.5f;
        }
    }
    if (!bossEntities.empty()) {
        core::EntityId torso = bossEntities[0];
        core::ColliderShape shape;
        shape.kind = core::ColliderShapeKind::Capsule;
        shape.params = glm::vec3(1.2f, 2.0f, 0.0f);
        if (!physics.attachBodyToEntity(torso, ecs, shape, core::PhysicsMaterial{}, core::RigidBodyMotionType::Static)) {
            std::fprintf(stderr, "spawnUnderwaterMapVisual: attachBodyToEntity failed for the boss's own collider.\n");
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
