#include "tntwars/PvPNode.hpp"

#include <algorithm>

#include "core/Components.hpp"

namespace engine::tntwars {

void tickPvPNodeCapture(PvPNodeState& node, bool teamAPresent, bool teamBPresent, float dt) {
    if (dt <= 0.0f) return;
    if (teamAPresent && teamBPresent) return; // real, contested -- no progress change
    if (!teamAPresent && !teamBPresent) return; // real, uncontested-empty -- holds, no decay

    TeamId presentTeam = teamAPresent ? TeamId::A : TeamId::B;
    if (node.capturingTeam != presentTeam) {
        node.capturingTeam = presentTeam;
        node.captureProgress = 0.0f;
    }

    node.captureProgress = std::min(1.0f, node.captureProgress + dt / kPvPNodeCaptureSeconds);
    if (node.captureProgress >= 1.0f) {
        node.controllingTeam = presentTeam;
    }
}

core::EntityId spawnPvPNodeVisual(core::ECS& ecs, core::MeshLibrary& meshLibrary,
                                    const core::ProceduralMaterialLibrary& materials, VmaAllocator allocator,
                                    VkDevice device, VkCommandPool cmdPool, VkQueue queue, const PvPNodeState& node,
                                    const char* name) {
    glm::vec3 halfExtents(node.radius * 0.9f, 0.15f, node.radius * 0.9f);
    core::Mesh mesh = core::Mesh::createBox(allocator, device, cmdPool, queue, halfExtents);
    if (mesh.vertexBuffer() == VK_NULL_HANDLE) return core::kNullEntity;
    uint32_t meshHandle = meshLibrary.registerMesh(std::move(mesh));

    core::EntityId entity = ecs.createEntity(name);
    if (auto* transform = ecs.tryGetComponent<core::Transform>(entity)) {
        transform->position = node.position + glm::vec3(0.0f, halfExtents.y, 0.0f);
    }

    auto& renderable = ecs.addComponent<core::Renderable>(entity);
    renderable.meshHandle = meshHandle;
    renderable.baseColor = glm::vec4(0.8f, 0.8f, 0.85f, 1.0f);
    renderable.metallic = 0.7f;
    renderable.roughness = 0.3f;
    renderable.albedoTexture = materials.metal.albedo;
    renderable.normalTexture = materials.metal.normal;
    renderable.metallicTexture = materials.metal.metallic;
    renderable.roughnessTexture = materials.metal.roughness;
    renderable.aoTexture = materials.metal.ao;
    renderable.emissiveColor = glm::vec3(0.8f, 0.8f, 0.85f);
    renderable.emissiveIntensity = 0.4f;

    auto& meshSource = ecs.addComponent<core::MeshSource>(entity);
    meshSource.kind = core::MeshSourceKind::Box;
    meshSource.params = halfExtents;

    return entity;
}

void tickPvPNodeVisual(const PvPNodeState& node, core::EntityId entity, core::ECS& ecs) {
    if (entity == core::kNullEntity) return;
    auto* renderable = ecs.tryGetComponent<core::Renderable>(entity);
    if (renderable == nullptr) return;

    constexpr glm::vec3 kNeutralColor(0.8f, 0.8f, 0.85f);
    constexpr glm::vec3 kTeamAColor(0.25f, 0.55f, 0.95f);
    constexpr glm::vec3 kTeamBColor(0.95f, 0.3f, 0.25f);

    glm::vec3 color = kNeutralColor;
    if (node.capturingTeam.has_value()) {
        glm::vec3 teamColor = *node.capturingTeam == TeamId::A ? kTeamAColor : kTeamBColor;
        color = glm::mix(kNeutralColor, teamColor, node.captureProgress);
    }
    if (node.controllingTeam.has_value()) {
        color = *node.controllingTeam == TeamId::A ? kTeamAColor : kTeamBColor;
    }

    renderable->baseColor = glm::vec4(color, 1.0f);
    renderable->emissiveColor = color;
}

} // namespace engine::tntwars
