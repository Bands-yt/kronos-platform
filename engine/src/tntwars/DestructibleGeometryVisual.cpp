#include "tntwars/DestructibleGeometryVisual.hpp"

#include <algorithm>
#include <cstdio>

#include "core/Components.hpp"

namespace engine::tntwars {

std::vector<DestructibleSegmentVisual> spawnDestructibleWallVisual(
    core::ECS& ecs, core::MeshLibrary& meshLibrary, core::Physics& physics, const core::PbrTextureSet& material,
    VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue,
    const std::vector<DestructibleSegment>& segments) {
    std::vector<DestructibleSegmentVisual> visuals;
    visuals.reserve(segments.size());

    for (const DestructibleSegment& segment : segments) {
        DestructibleSegmentVisual visual;

        core::Mesh mesh = core::Mesh::createBox(allocator, device, cmdPool, queue, segment.halfExtents);
        if (mesh.vertexBuffer() == VK_NULL_HANDLE) {
            visuals.push_back(visual); // real, honest partial failure -- see this function's own header comment
            continue;
        }
        uint32_t meshHandle = meshLibrary.registerMesh(std::move(mesh));

        core::EntityId entity = ecs.createEntity("DestructibleSegment");
        if (auto* transform = ecs.tryGetComponent<core::Transform>(entity)) transform->position = segment.position;

        auto& renderable = ecs.addComponent<core::Renderable>(entity);
        renderable.meshHandle = meshHandle;
        renderable.baseColor = glm::vec4(1.0f);
        renderable.metallic = 0.05f;
        renderable.roughness = 0.9f;
        renderable.albedoTexture = material.albedo;
        renderable.normalTexture = material.normal;
        renderable.metallicTexture = material.metallic;
        renderable.roughnessTexture = material.roughness;
        renderable.aoTexture = material.ao;

        auto& meshSource = ecs.addComponent<core::MeshSource>(entity);
        meshSource.kind = core::MeshSourceKind::Box;
        meshSource.params = segment.halfExtents;

        core::ColliderShape shape;
        shape.kind = core::ColliderShapeKind::Box;
        shape.params = segment.halfExtents;
        if (!physics.attachBodyToEntity(entity, ecs, shape, core::PhysicsMaterial{}, core::RigidBodyMotionType::Static)) {
            std::fprintf(stderr, "spawnDestructibleWallVisual: attachBodyToEntity failed for a real segment -- it will render but not collide.\n");
        }

        visual.entity = entity;
        visuals.push_back(visual);
    }

    return visuals;
}

void tickDestructibleWallVisual(std::vector<DestructibleSegment>& segments,
                                 std::vector<DestructibleSegmentVisual>& visuals, core::ECS& ecs, core::Physics& physics,
                                 float dt) {
    size_t count = std::min(segments.size(), visuals.size());
    for (size_t i = 0; i < count; ++i) {
        DestructibleSegment& segment = segments[i];
        DestructibleSegmentVisual& visual = visuals[i];
        if (visual.entity == core::kNullEntity) continue; // real spawn failure earlier -- nothing real to sync

        if (!visual.destroyed) {
            if (isSegmentDestroyed(segment)) {
                visual.destroyed = true;
                visual.rebuildTimer = kDestructibleRebuildDelaySeconds;
                physics.detachBody(visual.entity, ecs);
                if (auto* renderable = ecs.tryGetComponent<core::Renderable>(visual.entity)) renderable->visible = false;
            }
            continue;
        }

        visual.rebuildTimer -= dt;
        if (visual.rebuildTimer > 0.0f) continue;

        // Real, complete rebuild -- health, physics, and visibility all
        // restored together, not just one of the three.
        segment.health = segment.maxHealth;
        visual.destroyed = false;
        visual.rebuildTimer = 0.0f;
        core::ColliderShape shape;
        shape.kind = core::ColliderShapeKind::Box;
        shape.params = segment.halfExtents;
        if (!physics.attachBodyToEntity(visual.entity, ecs, shape, core::PhysicsMaterial{}, core::RigidBodyMotionType::Static)) {
            std::fprintf(stderr, "tickDestructibleWallVisual: attachBodyToEntity failed while rebuilding a real segment -- it will render but not collide.\n");
        }
        if (auto* renderable = ecs.tryGetComponent<core::Renderable>(visual.entity)) renderable->visible = true;
    }
}

} // namespace engine::tntwars
