#include "tntwars/MapLayoutVisual.hpp"

#include <cstdio>

#include "core/Components.hpp"
#include "tntwars/MapMaterial.hpp"

namespace engine::tntwars {

std::vector<core::EntityId> spawnMapLayoutVisual(core::ECS& ecs, core::Physics& physics, core::MeshLibrary& meshLibrary,
                                                   const core::ProceduralMaterialLibrary& materials, VmaAllocator allocator,
                                                   VkDevice device, VkCommandPool cmdPool, VkQueue queue, MapId map) {
    std::vector<core::EntityId> spawned;
    std::vector<MapLayoutPiece> pieces = buildMapLayout(map);
    spawned.reserve(pieces.size());

    for (const MapLayoutPiece& piece : pieces) {
        core::Mesh mesh = piece.shape == MapPieceShape::Box
                               ? core::Mesh::createBox(allocator, device, cmdPool, queue, piece.halfExtents)
                               : core::Mesh::createPlane(allocator, device, cmdPool, queue, piece.halfExtents.x,
                                                          piece.halfExtents.z);
        if (mesh.vertexBuffer() == VK_NULL_HANDLE) continue;
        uint32_t meshHandle = meshLibrary.registerMesh(std::move(mesh));

        core::EntityId entity = ecs.createEntity(piece.name);
        if (auto* transform = ecs.tryGetComponent<core::Transform>(entity)) transform->position = piece.position;

        auto& renderable = ecs.addComponent<core::Renderable>(entity);
        renderable.meshHandle = meshHandle;
        renderable.baseColor = glm::vec4(piece.color, 1.0f);
        materials.applyTo(renderable, classifyMapPieceMaterial(piece.name));

        auto& meshSource = ecs.addComponent<core::MeshSource>(entity);
        meshSource.kind = piece.shape == MapPieceShape::Box ? core::MeshSourceKind::Box : core::MeshSourceKind::Plane;
        meshSource.params = piece.halfExtents;

        // The one real addition TntWarsPlugin::buildMapGeometry() itself
        // deliberately never makes -- see this file's own header comment.
        // A Plane piece's real collider is a real, thin (0.05 half-height)
        // Box rather than an infinite Jolt half-space plane: this engine's
        // ColliderShapeKind has no analytic "plane" primitive at all (see
        // Components.hpp's own enum), and every Plane piece here already
        // represents a real, finite play surface (ground, a base pad),
        // not a true infinite plane, so a thin box is the real, correct
        // shape, not an approximation of a missing feature.
        core::ColliderShape shape;
        shape.kind = core::ColliderShapeKind::Box;
        shape.params = piece.shape == MapPieceShape::Box ? piece.halfExtents
                                                           : glm::vec3(piece.halfExtents.x, 0.05f, piece.halfExtents.z);
        if (!physics.attachBodyToEntity(entity, ecs, shape, core::PhysicsMaterial{}, core::RigidBodyMotionType::Static)) {
            std::fprintf(stderr, "spawnMapLayoutVisual: attachBodyToEntity failed for piece \"%s\" -- it will render but not collide.\n",
                          piece.name.c_str());
        }

        spawned.push_back(entity);
    }

    return spawned;
}

} // namespace engine::tntwars
