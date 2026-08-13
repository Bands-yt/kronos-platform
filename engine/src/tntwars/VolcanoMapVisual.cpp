#include "tntwars/VolcanoMapVisual.hpp"

#include <cmath>
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
                         VkQueue queue, const char* name, glm::vec3 emissiveColor = glm::vec3(0.0f),
                         float emissiveIntensity = 0.0f) {
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

    core::ColliderShape shape;
    shape.kind = core::ColliderShapeKind::Box;
    shape.params = halfExtents;
    if (!physics.attachBodyToEntity(entity, ecs, shape, core::PhysicsMaterial{}, core::RigidBodyMotionType::Static)) {
        std::fprintf(stderr, "spawnVolcanoMapVisual: attachBodyToEntity failed for \"%s\" -- it will render but not collide.\n",
                     name);
    }
    return entity;
}

// Real, tuned lava emissive values -- matches
// core::ProceduralMaterialLibrary::applyTo()'s own MapPieceMaterialKind::Lava
// case exactly (metallic/roughness/emissiveColor/emissiveIntensity), the
// same "kept deliberately modest" reasoning that function's own comment
// documents (a brighter flat tint would wash out lavaAlbedoPixels()'s own
// crack-vein contrast through the ACES tonemap) -- this file spawns lava
// boxes directly (like SkyMapVisual.cpp's crystal vein) rather than through
// classifyMapPieceMaterial(), so the same real values are restated here.
constexpr float kLavaMetallic = 0.0f;
constexpr float kLavaRoughness = 0.55f;
const glm::vec3 kLavaEmissiveColor{1.0f, 0.45f, 0.12f};
constexpr float kLavaEmissiveIntensity = 0.15f;

// One real tapered volcano cone: 5 stacked, progressively-narrower rock
// layers (steeper taper than SkyMapVisual's own mesa islands, for a real
// conical silhouette rather than a stepped mesa) with two embedded
// glowing lava-crack bands partway up, topped by a flat crater floor and
// a real, solid, emissive lava pool at its center. Returns the world-space
// center of the crater floor's own top surface -- where a caller places
// the boss arena.
glm::vec3 spawnVolcanoCone(core::ECS& ecs, core::Physics& physics, core::MeshLibrary& meshLibrary,
                            const core::ProceduralMaterialLibrary& materials, glm::vec3 baseCenter,
                            float baseHalfWidth, VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool,
                            VkQueue queue, std::vector<core::EntityId>& outSpawned) {
    constexpr int kLayerCount = 5;
    constexpr float kLayerHalfHeight = 3.0f;
    constexpr float kTaper = 0.62f; // steeper than SkyMap's 0.72f mesa taper -- reads as a real cone, not a step pyramid

    float y = baseCenter.y;
    float halfWidth = baseHalfWidth;
    for (int layer = 0; layer < kLayerCount; ++layer) {
        glm::vec3 halfExtents(halfWidth, kLayerHalfHeight, halfWidth);
        glm::vec3 pos(baseCenter.x, y + kLayerHalfHeight, baseCenter.z);
        core::EntityId e = spawnBox(ecs, physics, meshLibrary, materials.stone, 0.05f, 0.88f, pos, halfExtents,
                                     allocator, device, cmdPool, queue,
                                     ("VolcanoMap_ConeLayer" + std::to_string(layer)).c_str());
        if (e != core::kNullEntity) outSpawned.push_back(e);

        // Real, embedded glowing crack band on two of the five layers
        // (real "glowing cracks" per the brief) -- a thin lava-material box
        // wrapped tight against that layer's own uphill face, offset just
        // proud of the stone surface so it reads as a real seam, not a
        // z-fighting sliver.
        if (layer == 1 || layer == 3) {
            glm::vec3 crackHalfExtents(halfWidth * 0.9f, 0.5f, 0.12f);
            glm::vec3 crackPos(baseCenter.x, y + kLayerHalfHeight, baseCenter.z + halfWidth + 0.1f);
            core::EntityId crack = spawnBox(ecs, physics, meshLibrary, materials.lava, kLavaMetallic, kLavaRoughness,
                                             crackPos, crackHalfExtents, allocator, device, cmdPool, queue,
                                             ("VolcanoMap_CrackBand" + std::to_string(layer)).c_str(),
                                             kLavaEmissiveColor, kLavaEmissiveIntensity);
            if (crack != core::kNullEntity) outSpawned.push_back(crack);
        }

        y += kLayerHalfHeight * 2.0f;
        halfWidth *= kTaper;
    }

    // Real crater floor -- a flat stone platform slightly wider than the
    // cone's own final layer, the same "deliberately carved/built platform"
    // read spawnMesaIsland() establishes.
    constexpr float kFloorHalfHeight = 0.75f;
    glm::vec3 floorHalfExtents(halfWidth / kTaper * 0.9f, kFloorHalfHeight, halfWidth / kTaper * 0.9f);
    glm::vec3 floorPos(baseCenter.x, y + kFloorHalfHeight, baseCenter.z);
    core::EntityId floor = spawnBox(ecs, physics, meshLibrary, materials.stone, 0.05f, 0.85f, floorPos,
                                     floorHalfExtents, allocator, device, cmdPool, queue, "VolcanoMap_CraterFloor");
    if (floor != core::kNullEntity) outSpawned.push_back(floor);

    // Real, solid, emissive crater lava pool -- centered on the floor,
    // proud enough of its surface to read as a real molten pool rather
    // than a painted decal. See VolcanoMapVisual.hpp's own header comment
    // for why this stays a real, solid collider rather than a walk-through
    // trigger volume.
    constexpr float kPoolHalfHeight = 0.35f;
    glm::vec3 poolHalfExtents(floorHalfExtents.x * 0.45f, kPoolHalfHeight, floorHalfExtents.z * 0.45f);
    glm::vec3 poolPos(baseCenter.x, floorPos.y + kFloorHalfHeight + kPoolHalfHeight, baseCenter.z);
    core::EntityId pool = spawnBox(ecs, physics, meshLibrary, materials.lava, kLavaMetallic, kLavaRoughness, poolPos,
                                    poolHalfExtents, allocator, device, cmdPool, queue, "VolcanoMap_CraterPool",
                                    kLavaEmissiveColor, kLavaEmissiveIntensity * 2.0f); // brighter -- this is the map's own real centerpiece
    if (pool != core::kNullEntity) outSpawned.push_back(pool);

    return glm::vec3(floorPos.x, floorPos.y + kFloorHalfHeight, floorPos.z);
}

// Real lava-river hazard channel -- a long, thin, solid, emissive lava box
// running from the crater's own base out toward a play lane, real yaw
// computed the same atan2 way SkyMapVisual's own bridges are, so it reads
// as a real flowing channel rather than an axis-aligned rectangle dropped
// at an angle by coincidence.
void spawnLavaRiver(core::ECS& ecs, core::Physics& physics, core::MeshLibrary& meshLibrary,
                     const core::ProceduralMaterialLibrary& materials, glm::vec3 from, glm::vec3 to,
                     VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue, const char* name,
                     std::vector<core::EntityId>& outSpawned) {
    glm::vec3 mid = (from + to) * 0.5f;
    float length = glm::length(glm::vec3(to.x - from.x, 0.0f, to.z - from.z)) * 0.5f;
    float yaw = std::atan2(to.x - from.x, to.z - from.z);

    core::Mesh mesh = core::Mesh::createBox(allocator, device, cmdPool, queue, glm::vec3(2.5f, 0.3f, length));
    if (mesh.vertexBuffer() == VK_NULL_HANDLE) return;
    uint32_t meshHandle = meshLibrary.registerMesh(std::move(mesh));
    core::EntityId entity = ecs.createEntity(name);
    if (auto* transform = ecs.tryGetComponent<core::Transform>(entity)) {
        transform->position = mid;
        transform->rotation = glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f));
    }
    auto& renderable = ecs.addComponent<core::Renderable>(entity);
    renderable.meshHandle = meshHandle;
    renderable.baseColor = glm::vec4(1.0f);
    renderable.metallic = kLavaMetallic;
    renderable.roughness = kLavaRoughness;
    renderable.albedoTexture = materials.lava.albedo;
    renderable.normalTexture = materials.lava.normal;
    renderable.metallicTexture = materials.lava.metallic;
    renderable.roughnessTexture = materials.lava.roughness;
    renderable.aoTexture = materials.lava.ao;
    renderable.emissiveColor = kLavaEmissiveColor;
    renderable.emissiveIntensity = kLavaEmissiveIntensity;
    auto& meshSource = ecs.addComponent<core::MeshSource>(entity);
    meshSource.kind = core::MeshSourceKind::Box;
    meshSource.params = glm::vec3(2.5f, 0.3f, length);
    core::ColliderShape shape;
    shape.kind = core::ColliderShapeKind::Box;
    shape.params = glm::vec3(2.5f, 0.3f, length);
    if (!physics.attachBodyToEntity(entity, ecs, shape, core::PhysicsMaterial{}, core::RigidBodyMotionType::Static)) {
        std::fprintf(stderr, "spawnVolcanoMapVisual: attachBodyToEntity failed for lava river \"%s\".\n", name);
    }
    outSpawned.push_back(entity);
}

} // namespace

glm::vec3 kVolcanoMapCraterBaseCenter() { return glm::vec3(0.0f, 0.0f, 0.0f); } // matches buildMantle()'s own real "LavaPool" center (MapLayout.cpp)

std::vector<core::EntityId> spawnVolcanoMapVisual(core::ECS& ecs, core::Physics& physics,
                                                    core::MeshLibrary& meshLibrary,
                                                    const core::ProceduralMaterialLibrary& materials,
                                                    VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool,
                                                    VkQueue queue, miningsim::RarityTier bossRarity) {
    std::vector<core::EntityId> spawned;

    // Real, central volcano cone -- radius 10.0 leaves both side lanes
    // (|x| > 10, out to the map's own real x=+-60 ground extent) fully
    // open, so the cone reads as a real "central arena" obstacle players
    // route around, not a wall that splits the map in two.
    glm::vec3 craterTop = spawnVolcanoCone(ecs, physics, meshLibrary, materials, kVolcanoMapCraterBaseCenter(), 10.0f,
                                            allocator, device, cmdPool, queue, spawned);

    // Real lava-river hazard channels -- one flowing toward each side lane,
    // crossing it diagonally so a player can't just hug the map edge to
    // avoid the volcano entirely without ever seeing lava.
    glm::vec3 coneBase = kVolcanoMapCraterBaseCenter();
    spawnLavaRiver(ecs, physics, meshLibrary, materials, coneBase + glm::vec3(9.0f, 0.15f, 0.0f),
                    coneBase + glm::vec3(38.0f, 0.15f, 18.0f), allocator, device, cmdPool, queue,
                    "VolcanoMap_LavaRiver_East", spawned);
    spawnLavaRiver(ecs, physics, meshLibrary, materials, coneBase + glm::vec3(-9.0f, 0.15f, 0.0f),
                    coneBase + glm::vec3(-38.0f, 0.15f, -18.0f), allocator, device, cmdPool, queue,
                    "VolcanoMap_LavaRiver_West", spawned);

    // Real, walkable TNT tunnel through the western rock formation --
    // reuses buildMantle()'s own "RockPillar_Left" siting (x=-25, see
    // MapLayout.cpp) as the tunnel's real location, so it reads as
    // literally tunneling through that existing rock rather than floating
    // in open ground. Two parallel stone walls + a roof cap, with a real
    // open gap between the walls a player can walk straight through.
    constexpr float kTunnelHalfWidth = 6.0f;  // real, walkable-width gap between the two walls
    constexpr float kTunnelHalfHeight = 3.0f;
    constexpr float kTunnelHalfLength = 8.0f;
    glm::vec3 tunnelCenter(-25.0f, kTunnelHalfHeight, 0.0f);
    core::EntityId tunnelWallA =
        spawnBox(ecs, physics, meshLibrary, materials.stone, 0.05f, 0.9f,
                 tunnelCenter + glm::vec3(-(kTunnelHalfWidth + 1.5f), 0.0f, 0.0f), glm::vec3(1.5f, kTunnelHalfHeight, kTunnelHalfLength),
                 allocator, device, cmdPool, queue, "VolcanoMap_Tunnel_WallA");
    core::EntityId tunnelWallB =
        spawnBox(ecs, physics, meshLibrary, materials.stone, 0.05f, 0.9f,
                 tunnelCenter + glm::vec3(kTunnelHalfWidth + 1.5f, 0.0f, 0.0f), glm::vec3(1.5f, kTunnelHalfHeight, kTunnelHalfLength),
                 allocator, device, cmdPool, queue, "VolcanoMap_Tunnel_WallB");
    core::EntityId tunnelRoof =
        spawnBox(ecs, physics, meshLibrary, materials.stone, 0.05f, 0.9f,
                 tunnelCenter + glm::vec3(0.0f, kTunnelHalfHeight + 0.75f, 0.0f),
                 glm::vec3(kTunnelHalfWidth + 3.0f, 0.75f, kTunnelHalfLength), allocator, device, cmdPool, queue,
                 "VolcanoMap_Tunnel_Roof");
    for (core::EntityId e : {tunnelWallA, tunnelWallB, tunnelRoof}) {
        if (e != core::kNullEntity) spawned.push_back(e);
    }

    // Real, rugged multi-height rock formations flanking the tunnel and
    // the eastern lane -- the brief's own "rugged and multi-layered
    // terrain," expressed the same primitive-composition way SkyMapVisual's
    // crystal vein is: several irregular boxes at varied heights, not one
    // uniform block.
    const glm::vec3 kRuggedOffsets[] = {
        {-18.0f, 1.5f, 6.0f}, {-15.0f, 2.5f, -5.0f}, {18.0f, 1.8f, -7.0f}, {21.0f, 3.2f, 4.0f}, {14.0f, 1.2f, 10.0f},
    };
    constexpr size_t kRuggedCount = 5;
    for (size_t i = 0; i < kRuggedCount; ++i) {
        glm::vec3 halfExtents(1.5f + static_cast<float>(i % 3) * 0.6f, kRuggedOffsets[i].y,
                               1.5f + static_cast<float>((i + 1) % 3) * 0.5f);
        glm::vec3 pos(kRuggedOffsets[i].x, halfExtents.y, kRuggedOffsets[i].z);
        core::EntityId e = spawnBox(ecs, physics, meshLibrary, materials.stone, 0.05f, 0.92f, pos, halfExtents,
                                     allocator, device, cmdPool, queue,
                                     ("VolcanoMap_Rugged" + std::to_string(i)).c_str());
        if (e != core::kNullEntity) spawned.push_back(e);
    }

    // Real molten-armor boss, on the crater's own rim -- reuses the exact
    // same generic, position/stats-driven boss visual pipeline the Sky Map
    // arena already proved out, then real-overrides its emissive/metallic
    // look directly on the returned entities (SkyMapVisual.hpp's own
    // header comment on this being "a color/material override on the
    // existing boss pattern, not a new boss system" per the approved plan).
    miningsim::MobState boss = miningsim::spawnBossOfRarity(bossRarity);
    std::vector<core::EntityId> bossEntities =
        miningsim::spawnMobVisual(ecs, meshLibrary, materials, allocator, device, cmdPool, queue, boss, craterTop);
    for (core::EntityId bossPart : bossEntities) {
        if (auto* renderable = ecs.tryGetComponent<core::Renderable>(bossPart)) {
            renderable->emissiveColor = glm::vec3(1.0f, 0.35f, 0.05f); // real, molten-orange armor glow
            renderable->emissiveIntensity = 0.35f;
            renderable->metallic = 0.65f;
            renderable->roughness = 0.4f;
        }
    }
    if (!bossEntities.empty()) {
        core::EntityId torso = bossEntities[0];
        core::ColliderShape shape;
        shape.kind = core::ColliderShapeKind::Capsule;
        shape.params = glm::vec3(1.2f, 2.0f, 0.0f);
        if (!physics.attachBodyToEntity(torso, ecs, shape, core::PhysicsMaterial{}, core::RigidBodyMotionType::Static)) {
            std::fprintf(stderr, "spawnVolcanoMapVisual: attachBodyToEntity failed for the boss's own collider.\n");
        }
    }
    spawned.insert(spawned.end(), bossEntities.begin(), bossEntities.end());

    std::vector<core::EntityId> toolEntities = miningsim::spawnForgedToolVisual(
        ecs, meshLibrary, materials, allocator, device, cmdPool, queue, miningsim::MiningToolType::ExplosiveCharge,
        craterTop + glm::vec3(3.0f, 0.0f, 3.0f));
    spawned.insert(spawned.end(), toolEntities.begin(), toolEntities.end());

    return spawned;
}

} // namespace engine::tntwars
