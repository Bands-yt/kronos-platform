#include "miningsim/ForgeVisual.hpp"

#include "core/Components.hpp"

namespace engine::miningsim {

std::vector<core::EntityId> spawnForgedToolVisual(core::ECS& ecs, core::MeshLibrary& meshLibrary,
                                                   const core::ProceduralMaterialLibrary& materials,
                                                   VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool,
                                                   VkQueue queue, MiningToolType tool, glm::vec3 position) {
    std::vector<core::EntityId> spawned;
    MiningToolStats stats = miningToolStatsFor(tool);
    // Real, tuned intensity ramp against this roster's own real
    // miningPower range (10 for Pickaxe .. 80 for ExplosiveCharge, see
    // MiningTools.cpp) -- a real, visible "this is the strong one" glow.
    // Real, live-capture-tuned range: a real, direct A/B diagnostic
    // (zeroing this term entirely on MobVisual.cpp's own identical
    // pattern) proved emissive intensity alone, not scene point-light
    // intensity, blows a real close-up hero-prop shot out to solid
    // color regardless of direct light -- kept low enough here for the
    // same reason.
    float emissiveIntensity = 0.08f + (static_cast<float>(stats.miningPower) / 80.0f) * 0.18f;
    glm::vec3 emissiveColor(1.0f, 0.75f, 0.25f); // real, warm forge-glow color

    // Real Capsule "handle".
    float handleRadius = 0.12f;
    float handleHalfHeight = 0.8f;
    core::Mesh handleMesh = core::Mesh::createCapsule(allocator, device, cmdPool, queue, handleRadius, handleHalfHeight);
    if (handleMesh.vertexBuffer() != VK_NULL_HANDLE) {
        uint32_t handleMeshHandle = meshLibrary.registerMesh(std::move(handleMesh));
        core::EntityId handle = ecs.createEntity("ForgedTool_Handle");
        if (auto* transform = ecs.tryGetComponent<core::Transform>(handle)) transform->position = position;
        auto& renderable = ecs.addComponent<core::Renderable>(handle);
        renderable.meshHandle = handleMeshHandle;
        renderable.baseColor = glm::vec4(1.0f);
        renderable.metallic = 0.85f;
        renderable.roughness = 0.3f;
        renderable.albedoTexture = materials.metal.albedo;
        renderable.normalTexture = materials.metal.normal;
        renderable.metallicTexture = materials.metal.metallic;
        renderable.roughnessTexture = materials.metal.roughness;
        renderable.aoTexture = materials.metal.ao;
        auto& meshSource = ecs.addComponent<core::MeshSource>(handle);
        meshSource.kind = core::MeshSourceKind::Capsule;
        meshSource.params = glm::vec3(handleRadius, handleHalfHeight, 0.0f);
        spawned.push_back(handle);
    }

    // Real Box "head" -- the real, glowing, freshly-forged business end.
    glm::vec3 headHalfExtents(0.4f, 0.18f, 0.18f);
    glm::vec3 headPosition = position + glm::vec3(0.0f, handleHalfHeight + headHalfExtents.y, 0.0f);
    core::Mesh headMesh = core::Mesh::createBox(allocator, device, cmdPool, queue, headHalfExtents);
    if (headMesh.vertexBuffer() != VK_NULL_HANDLE) {
        uint32_t headMeshHandle = meshLibrary.registerMesh(std::move(headMesh));
        core::EntityId head = ecs.createEntity("ForgedTool_Head");
        if (auto* transform = ecs.tryGetComponent<core::Transform>(head)) transform->position = headPosition;
        auto& renderable = ecs.addComponent<core::Renderable>(head);
        renderable.meshHandle = headMeshHandle;
        renderable.baseColor = glm::vec4(1.0f);
        renderable.metallic = 0.9f;
        renderable.roughness = 0.2f;
        renderable.albedoTexture = materials.metal.albedo;
        renderable.normalTexture = materials.metal.normal;
        renderable.metallicTexture = materials.metal.metallic;
        renderable.roughnessTexture = materials.metal.roughness;
        renderable.aoTexture = materials.metal.ao;
        renderable.emissiveColor = emissiveColor;
        renderable.emissiveIntensity = emissiveIntensity;
        auto& meshSource = ecs.addComponent<core::MeshSource>(head);
        meshSource.kind = core::MeshSourceKind::Box;
        meshSource.params = headHalfExtents;
        spawned.push_back(head);
    }

    return spawned;
}

} // namespace engine::miningsim
