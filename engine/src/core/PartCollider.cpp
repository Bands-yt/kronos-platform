#include "core/PartCollider.hpp"

#include <algorithm>
#include <cmath>

namespace engine::core {

namespace {
bool sameCollider(const ColliderShape& a, const ColliderShape& b) {
    return a.kind == b.kind && glm::all(glm::lessThan(glm::abs(a.params - b.params), glm::vec3(1e-4f)));
}
} // namespace

std::optional<ColliderShape> colliderForMeshSource(const MeshSource& mesh, glm::vec3 scale) {
    scale = glm::abs(scale);
    ColliderShape shape;
    switch (mesh.kind) {
        case MeshSourceKind::Box:
            shape.kind = ColliderShapeKind::Box;
            shape.params = mesh.params * scale;
            break;
        case MeshSourceKind::Plane:
            shape.kind = ColliderShapeKind::Box;
            shape.params = {mesh.params.x * scale.x, 0.05f, mesh.params.z * scale.z};
            break;
        case MeshSourceKind::Capsule: {
            const bool uniform = std::fabs(scale.x - scale.y) < 1e-4f && std::fabs(scale.x - scale.z) < 1e-4f;
            if (mesh.params.y <= 0.0f && uniform) {
                shape.kind = ColliderShapeKind::Sphere;
                shape.params = {mesh.params.x * scale.x, 0.0f, 0.0f};
            } else if (mesh.params.y <= 0.0f) {
                shape.kind = ColliderShapeKind::Box;
                shape.params = mesh.params.x * scale;
            } else {
                shape.kind = ColliderShapeKind::Capsule;
                shape.params = {mesh.params.x * std::max(scale.x, scale.z), mesh.params.y * scale.y, 0.0f};
            }
            break;
        }
        default:
            return std::nullopt;
    }
    shape.params = glm::max(shape.params, glm::vec3(0.01f, shape.kind == ColliderShapeKind::Box ? 0.01f : 0.0f,
                                                    shape.kind == ColliderShapeKind::Box ? 0.01f : 0.0f));
    return shape;
}

void PartColliderSync::update(ECS& ecs) {
    auto view = ecs.raw().view<ColliderShape, MeshSource, Transform>();
    for (auto it = fitted_.begin(); it != fitted_.end();) {
        if (!ecs.raw().valid(it->first) || !view.contains(it->first)) it = fitted_.erase(it);
        else ++it;
    }
    for (EntityId entity : view) {
        auto& collider = view.get<ColliderShape>(entity);
        std::optional<ColliderShape> fit = colliderForMeshSource(view.get<MeshSource>(entity), view.get<Transform>(entity).scale);
        if (!fit) continue;
        auto known = fitted_.find(entity);
        if (known == fitted_.end()) {
            if (sameCollider(collider, *fit)) fitted_.emplace(entity, *fit);
            continue;
        }
        if (!sameCollider(collider, known->second)) {
            fitted_.erase(known);
            continue;
        }
        if (!sameCollider(collider, *fit)) {
            collider.kind = fit->kind;
            collider.params = fit->params;
            known->second = *fit;
        }
    }
}

} // namespace engine::core
