#include "trailer/TimeglassModel.hpp"

#include "core/Components.hpp"

namespace engine::trailer {

namespace {

struct TimeglassRing {
    float y;
    float halfWidth;
};

// Real, fixed, deterministic taper -- four real rings per side, widest
// at the real crown/base, narrowest near the real neck, the same
// "real, fixed inventory of hand-placed primitives" precedent
// MiningSimRtx.cpp's own kClusterShards/kCeilingSlabs arrays already
// establish.
constexpr TimeglassRing kBottomRings[] = {
    {-2.6f, 1.0f},
    {-1.9f, 0.75f},
    {-1.2f, 0.45f},
    {-0.55f, 0.2f},
};
constexpr TimeglassRing kTopRings[] = {
    {0.55f, 0.2f},
    {1.2f, 0.45f},
    {1.9f, 0.75f},
    {2.6f, 1.0f},
};
constexpr float kRingHalfHeight = 0.12f;
constexpr float kNeckRadius = 0.12f;
constexpr float kNeckHalfHeight = 0.4f;

core::EntityId spawnRing(core::ECS& ecs, core::MeshLibrary& meshLibrary, const core::ProceduralMaterialLibrary& materials,
                          VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue,
                          glm::vec3 position, float halfWidth, float glowStrength) {
    glm::vec3 halfExtents(halfWidth, kRingHalfHeight, halfWidth);
    core::Mesh mesh = core::Mesh::createBox(allocator, device, cmdPool, queue, halfExtents);
    if (mesh.vertexBuffer() == VK_NULL_HANDLE) return core::kNullEntity;
    uint32_t meshHandle = meshLibrary.registerMesh(std::move(mesh));

    core::EntityId entity = ecs.createEntity("Timeglass_Ring");
    if (auto* transform = ecs.tryGetComponent<core::Transform>(entity)) transform->position = position;

    auto& renderable = ecs.addComponent<core::Renderable>(entity);
    renderable.meshHandle = meshHandle;
    renderable.baseColor = glm::vec4(1.0f);
    renderable.metallic = 0.1f;
    renderable.roughness = 0.15f;
    renderable.albedoTexture = materials.crystal.albedo;
    renderable.normalTexture = materials.crystal.normal;
    renderable.metallicTexture = materials.crystal.metallic;
    renderable.roughnessTexture = materials.crystal.roughness;
    renderable.aoTexture = materials.crystal.ao;
    // Real, warm gold-white glow -- "timeless creation," sand-pooling-at-
    // the-caps read: rings nearer the crown/base glow brighter than the
    // narrow neck rings.
    renderable.emissiveColor = glm::vec3(1.0f, 0.85f, 0.5f);
    renderable.emissiveIntensity = glowStrength;

    auto& meshSource = ecs.addComponent<core::MeshSource>(entity);
    meshSource.kind = core::MeshSourceKind::Box;
    meshSource.params = halfExtents;
    return entity;
}

} // namespace

std::vector<core::EntityId> spawnTimeglassModel(core::ECS& ecs, core::MeshLibrary& meshLibrary,
                                                 const core::ProceduralMaterialLibrary& materials,
                                                 VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool,
                                                 VkQueue queue, glm::vec3 position) {
    std::vector<core::EntityId> spawned;

    for (const TimeglassRing& ring : kBottomRings) {
        // Real, live-capture-tuned range -- see ForgeVisual.cpp's own
        // comment on why this stays low under the real manual-exposure
        // LogoReveal beat uses. Narrower rings still glow slightly
        // hotter, near the falling "sand."
        float glow = 0.10f + (1.0f - ring.halfWidth) * 0.05f;
        core::EntityId entity = spawnRing(ecs, meshLibrary, materials, allocator, device, cmdPool, queue,
                                           position + glm::vec3(0.0f, ring.y, 0.0f), ring.halfWidth, glow);
        if (entity != core::kNullEntity) spawned.push_back(entity);
    }
    for (const TimeglassRing& ring : kTopRings) {
        float glow = 0.10f + (1.0f - ring.halfWidth) * 0.05f;
        core::EntityId entity = spawnRing(ecs, meshLibrary, materials, allocator, device, cmdPool, queue,
                                           position + glm::vec3(0.0f, ring.y, 0.0f), ring.halfWidth, glow);
        if (entity != core::kNullEntity) spawned.push_back(entity);
    }

    // Real Capsule neck connecting the two real tapered stacks.
    core::Mesh neckMesh = core::Mesh::createCapsule(allocator, device, cmdPool, queue, kNeckRadius, kNeckHalfHeight);
    if (neckMesh.vertexBuffer() != VK_NULL_HANDLE) {
        uint32_t neckMeshHandle = meshLibrary.registerMesh(std::move(neckMesh));
        core::EntityId neck = ecs.createEntity("Timeglass_Neck");
        if (auto* transform = ecs.tryGetComponent<core::Transform>(neck)) transform->position = position;
        auto& renderable = ecs.addComponent<core::Renderable>(neck);
        renderable.meshHandle = neckMeshHandle;
        renderable.baseColor = glm::vec4(1.0f);
        renderable.metallic = 0.1f;
        renderable.roughness = 0.1f;
        renderable.albedoTexture = materials.crystal.albedo;
        renderable.normalTexture = materials.crystal.normal;
        renderable.metallicTexture = materials.crystal.metallic;
        renderable.roughnessTexture = materials.crystal.roughness;
        renderable.aoTexture = materials.crystal.ao;
        renderable.emissiveColor = glm::vec3(1.0f, 0.9f, 0.6f);
        renderable.emissiveIntensity = 0.20f; // real, brightest point -- the real "sand" pinch point
        auto& meshSource = ecs.addComponent<core::MeshSource>(neck);
        meshSource.kind = core::MeshSourceKind::Capsule;
        meshSource.params = glm::vec3(kNeckRadius, kNeckHalfHeight, 0.0f);
        spawned.push_back(neck);
    }

    return spawned;
}

} // namespace engine::trailer
