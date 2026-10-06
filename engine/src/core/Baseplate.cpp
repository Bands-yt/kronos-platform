#include "core/Baseplate.hpp"

#include <vector>

#include "core/CollisionLayers.hpp"
#include "core/Components.hpp"
#include "core/Mesh.hpp"
#include "core/Physics.hpp"
#include "core/PhysicsMaterial.hpp"

namespace engine::core {
namespace {

constexpr float kHalfWidth = 250.0f;    // 500 units, X/Z
constexpr float kHalfHeight = 0.5f;     // 1 unit, Y
constexpr float kTileWorldSize = 10.0f;

// Same 24-vertex flat-shaded box layout as Mesh::createBox(), except top/
// bottom UVs are scaled to `t` instead of [0,1] so a tiled texture repeats.
Mesh createBaseplateMesh(VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue) {
    const glm::vec3 h{kHalfWidth, kHalfHeight, kHalfWidth};
    const float t = (kHalfWidth * 2.0f) / kTileWorldSize; // 50

    std::vector<Vertex> vertices = {
        {{h.x, -h.y, -h.z}, {1, 0, 0}, {0, 0}},  {{h.x, -h.y, h.z}, {1, 0, 0}, {1, 0}},
        {{h.x, h.y, h.z}, {1, 0, 0}, {1, 1}},    {{h.x, h.y, -h.z}, {1, 0, 0}, {0, 1}},
        {{-h.x, -h.y, h.z}, {-1, 0, 0}, {0, 0}}, {{-h.x, -h.y, -h.z}, {-1, 0, 0}, {1, 0}},
        {{-h.x, h.y, -h.z}, {-1, 0, 0}, {1, 1}}, {{-h.x, h.y, h.z}, {-1, 0, 0}, {0, 1}},
        {{-h.x, h.y, -h.z}, {0, 1, 0}, {0, 0}},  {{h.x, h.y, -h.z}, {0, 1, 0}, {t, 0}},
        {{h.x, h.y, h.z}, {0, 1, 0}, {t, t}},    {{-h.x, h.y, h.z}, {0, 1, 0}, {0, t}},
        {{-h.x, -h.y, h.z}, {0, -1, 0}, {0, 0}}, {{h.x, -h.y, h.z}, {0, -1, 0}, {t, 0}},
        {{h.x, -h.y, -h.z}, {0, -1, 0}, {t, t}}, {{-h.x, -h.y, -h.z}, {0, -1, 0}, {0, t}},
        {{h.x, -h.y, h.z}, {0, 0, 1}, {0, 0}},   {{-h.x, -h.y, h.z}, {0, 0, 1}, {1, 0}},
        {{-h.x, h.y, h.z}, {0, 0, 1}, {1, 1}},   {{h.x, h.y, h.z}, {0, 0, 1}, {0, 1}},
        {{-h.x, -h.y, -h.z}, {0, 0, -1}, {0, 0}}, {{h.x, -h.y, -h.z}, {0, 0, -1}, {1, 0}},
        {{h.x, h.y, -h.z}, {0, 0, -1}, {1, 1}},  {{-h.x, h.y, -h.z}, {0, 0, -1}, {0, 1}},
    };
    std::vector<uint32_t> indices;
    indices.reserve(36);
    for (uint32_t face = 0; face < 6; ++face) {
        uint32_t base = face * 4;
        indices.insert(indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }

    Mesh mesh;
    (void)mesh.uploadFromHost(allocator, device, cmdPool, queue, vertices, indices);
    return mesh;
}

} // namespace

EntityId spawnDefaultBaseplate(ECS& ecs, MeshLibrary& meshLibrary, VmaAllocator allocator, VkDevice device,
                                VkCommandPool cmdPool, VkQueue queue, Physics* physics) {
    uint32_t meshHandle = meshLibrary.registerMesh(createBaseplateMesh(allocator, device, cmdPool, queue));

    EntityId entity = ecs.createEntity("Baseplate"); // also adds Transform{} + Name{"Baseplate"}
    ecs.tryGetComponent<Transform>(entity)->position = {0.0f, -kHalfHeight, 0.0f};

    ecs.addComponent<MeshSource>(entity, MeshSource{MeshSourceKind::Box, {kHalfWidth, kHalfHeight, kHalfWidth}, ""});

    Renderable renderable;
    renderable.meshHandle = meshHandle;
    renderable.baseColor = {0.2f, 0.205f, 0.215f, 1.0f};
    renderable.layers.gridSize = 1.0f;
    renderable.metallic = 0.0f;
    renderable.roughness = 0.85f;
    ecs.addComponent<Renderable>(entity, renderable);

    ColliderShape shape{ColliderShapeKind::Box, {kHalfWidth, kHalfHeight, kHalfWidth}, ""};
    if (physics) {
        (void)physics->attachBodyToEntity(entity, ecs, shape, PhysicsMaterial{}, RigidBodyMotionType::Static, 0.0f,
                                           CollisionLayer::Static);
    } else {
        // No live Jolt world (Studio edit-mode): author the shape now.
        // RigidBody{kInvalidBodyId, Static} is required, not optional --
        // PhysicsPreviewPlugin::play() defaults any ColliderShape entity
        // with no RigidBody to Dynamic (see its own comment), which would
        // make the baseplate fall.
        ecs.addComponent<ColliderShape>(entity, shape);
        ecs.addComponent<PhysicsMaterial>(entity, PhysicsMaterial{});
        ecs.addComponent<RigidBody>(entity, RigidBody{RigidBody::kInvalidBodyId, RigidBodyMotionType::Static});
    }

    return entity;
}

} // namespace engine::core
