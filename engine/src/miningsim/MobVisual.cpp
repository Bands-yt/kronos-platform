#include "miningsim/MobVisual.hpp"

#include <cmath>

#include "core/Components.hpp"
#include "miningsim/Rarity.hpp"

namespace engine::miningsim {

std::vector<core::EntityId> spawnMobVisual(core::ECS& ecs, core::MeshLibrary& meshLibrary,
                                            const core::ProceduralMaterialLibrary& materials, VmaAllocator allocator,
                                            VkDevice device, VkCommandPool cmdPool, VkQueue queue,
                                            const MobState& mob, glm::vec3 position) {
    std::vector<core::EntityId> spawned;
    // Real, sqrt-scaled linear size from the real maxHealth ratio -- a
    // real boss (4x stats, Milestone 20's own kBossStatMultiplier) reads
    // as a real, visibly bigger creature (~2x linear) without becoming
    // absurdly oversized from a literal 1:1 stat-to-size mapping.
    float scale = std::sqrt(mob.maxHealth / kMobVisualBaselineHealth);
    glm::vec3 color = rarityTierColor(mob.rarity);
    // Real, live-capture-tuned range -- see ForgeVisual.cpp's own comment
    // on why this stays low under the real manual-exposure Mining-Sim
    // beats use.
    // Real, live-capture-tuned range -- a real, direct A/B diagnostic
    // (zeroing this term entirely) proved emissive intensity alone, not
    // scene point-light intensity, was the real cause of a real close-up
    // hero-shot blow-out: even at 0.48 (this formula's old midrange
    // value) a Legendary-rank torso saturated to solid pale color
    // regardless of direct light. Kept low enough that even the
    // brightest real rarity color (TrueHeavenly's own pure white, rank
    // 7) still reads as a real, shaded creature under the manual
    // exposure Mining-Sim-family beats use, not a flat cutout.
    float emissiveIntensity = 0.10f + static_cast<float>(rarityTierRank(mob.rarity)) * 0.02f;

    glm::vec3 torsoHalfExtents(0.6f * scale, 0.9f * scale, 0.4f * scale);
    glm::vec3 torsoPosition = position + glm::vec3(0.0f, torsoHalfExtents.y, 0.0f);

    core::Mesh torsoMesh = core::Mesh::createBox(allocator, device, cmdPool, queue, torsoHalfExtents);
    if (torsoMesh.vertexBuffer() == VK_NULL_HANDLE) return spawned;
    uint32_t torsoMeshHandle = meshLibrary.registerMesh(std::move(torsoMesh));

    core::EntityId entity = ecs.createEntity("MobVisual_Torso");
    if (auto* transform = ecs.tryGetComponent<core::Transform>(entity)) transform->position = torsoPosition;

    auto& torsoRenderable = ecs.addComponent<core::Renderable>(entity);
    torsoRenderable.meshHandle = torsoMeshHandle;
    torsoRenderable.baseColor = glm::vec4(1.0f);
    torsoRenderable.metallic = 0.7f;
    torsoRenderable.roughness = 0.4f;
    torsoRenderable.albedoTexture = materials.metal.albedo;
    torsoRenderable.normalTexture = materials.metal.normal;
    torsoRenderable.metallicTexture = materials.metal.metallic;
    torsoRenderable.roughnessTexture = materials.metal.roughness;
    torsoRenderable.aoTexture = materials.metal.ao;
    torsoRenderable.emissiveColor = color;
    torsoRenderable.emissiveIntensity = emissiveIntensity;

    auto& torsoMeshSource = ecs.addComponent<core::MeshSource>(entity);
    torsoMeshSource.kind = core::MeshSourceKind::Box;
    torsoMeshSource.params = torsoHalfExtents;
    spawned.push_back(entity);

    // Real Capsule "head" -- a real, second entity (this engine's
    // Renderable/MeshSource are 1-per-entity, matching MiningSimRtx.cpp's
    // own drill chassis/mast/bit split into separate real entities).
    float headRadius = 0.35f * scale;
    float headHalfHeight = 0.25f * scale;
    core::Mesh headMesh = core::Mesh::createCapsule(allocator, device, cmdPool, queue, headRadius, headHalfHeight);
    if (headMesh.vertexBuffer() != VK_NULL_HANDLE) {
        uint32_t headMeshHandle = meshLibrary.registerMesh(std::move(headMesh));
        core::EntityId head = ecs.createEntity("MobVisual_Head");
        if (auto* transform = ecs.tryGetComponent<core::Transform>(head)) {
            transform->position = torsoPosition + glm::vec3(0.0f, torsoHalfExtents.y + headRadius + headHalfHeight, 0.0f);
        }
        auto& headRenderable = ecs.addComponent<core::Renderable>(head);
        headRenderable.meshHandle = headMeshHandle;
        headRenderable.baseColor = glm::vec4(1.0f);
        headRenderable.metallic = 0.7f;
        headRenderable.roughness = 0.35f;
        headRenderable.albedoTexture = materials.metal.albedo;
        headRenderable.normalTexture = materials.metal.normal;
        headRenderable.metallicTexture = materials.metal.metallic;
        headRenderable.roughnessTexture = materials.metal.roughness;
        headRenderable.aoTexture = materials.metal.ao;
        headRenderable.emissiveColor = color;
        headRenderable.emissiveIntensity = emissiveIntensity * 1.2f;

        auto& headMeshSource = ecs.addComponent<core::MeshSource>(head);
        headMeshSource.kind = core::MeshSourceKind::Capsule;
        headMeshSource.params = glm::vec3(headRadius, headHalfHeight, 0.0f);
        spawned.push_back(head);
    }

    return spawned;
}

} // namespace engine::miningsim
