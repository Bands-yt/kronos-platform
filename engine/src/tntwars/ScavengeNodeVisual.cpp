#include "tntwars/ScavengeNodeVisual.hpp"

#include <algorithm>
#include <string>

#include "core/Components.hpp"
#include "core/Interactable.hpp"

namespace engine::tntwars {

std::vector<ScavengeNodeVisual> spawnScavengeNodeVisuals(core::ECS& ecs, core::MeshLibrary& meshLibrary,
                                                            const core::ProceduralMaterialLibrary& materials,
                                                            VmaAllocator allocator, VkDevice device,
                                                            VkCommandPool cmdPool, VkQueue queue,
                                                            const std::vector<ScavengeNodeState>& nodes, MapId map) {
    std::vector<ScavengeNodeVisual> visuals;
    visuals.reserve(nodes.size());

    for (size_t i = 0; i < nodes.size(); ++i) {
        const ScavengeNodeState& node = nodes[i];
        ScavengeNodeVisual visual;

        glm::vec3 halfExtents(0.3f, 0.45f, 0.3f);
        core::Mesh mesh = core::Mesh::createBox(allocator, device, cmdPool, queue, halfExtents);
        if (mesh.vertexBuffer() == VK_NULL_HANDLE) {
            visuals.push_back(visual);
            continue;
        }
        uint32_t meshHandle = meshLibrary.registerMesh(std::move(mesh));

        std::string name = "ScavengeNode_" + std::to_string(i);
        core::EntityId entity = ecs.createEntity(name.c_str());
        if (auto* transform = ecs.tryGetComponent<core::Transform>(entity)) {
            transform->position = node.position + glm::vec3(0.0f, halfExtents.y, 0.0f);
        }

        glm::vec3 tint = scavengeMaterialInfo(node.material).color;
        auto& renderable = ecs.addComponent<core::Renderable>(entity);
        renderable.meshHandle = meshHandle;
        renderable.baseColor = glm::vec4(tint, 1.0f);
        renderable.metallic = 0.1f;
        renderable.roughness = 0.2f;
        renderable.albedoTexture = materials.crystal.albedo;
        renderable.normalTexture = materials.crystal.normal;
        renderable.metallicTexture = materials.crystal.metallic;
        renderable.roughnessTexture = materials.crystal.roughness;
        renderable.aoTexture = materials.crystal.ao;
        renderable.emissiveColor = tint;
        renderable.emissiveIntensity = 0.5f;

        auto& meshSource = ecs.addComponent<core::MeshSource>(entity);
        meshSource.kind = core::MeshSourceKind::Box;
        meshSource.params = halfExtents;

        auto& interactable = ecs.addComponent<core::Interactable>(entity);
        interactable.prompt = std::string("Scavenge ") + mapMaterialDisplayName(map, node.material);
        interactable.proximityEnabled = true;
        interactable.proximityRadius = 2.5f;

        visual.entity = entity;
        visuals.push_back(visual);
    }

    return visuals;
}

void tickScavengeNodeVisuals(const std::vector<ScavengeNodeState>& nodes, std::vector<ScavengeNodeVisual>& visuals,
                              core::ECS& ecs) {
    size_t count = std::min(nodes.size(), visuals.size());
    for (size_t i = 0; i < count; ++i) {
        ScavengeNodeVisual& visual = visuals[i];
        if (visual.entity == core::kNullEntity) continue;
        bool depleted = nodes[i].quantityRemaining <= 0;
        if (depleted == visual.currentlyDepleted) continue;

        if (auto* renderable = ecs.tryGetComponent<core::Renderable>(visual.entity)) renderable->visible = !depleted;
        if (auto* interactable = ecs.tryGetComponent<core::Interactable>(visual.entity)) {
            interactable->proximityEnabled = !depleted;
        }
        visual.currentlyDepleted = depleted;
    }
}

} // namespace engine::tntwars
