#pragma once

#include <optional>
#include <unordered_map>

#include <glm/glm.hpp>

#include "core/Components.hpp"
#include "core/ECS.hpp"

// Roblox-style parts: every inserted part is solid, with a collider that
// matches what you see, and stays matched when you resize it.
namespace engine::core {

// The collider for a primitive mesh drawn at `scale`, or nothing for shapes
// without a simple match (a torus, an imported model).
[[nodiscard]] std::optional<ColliderShape> colliderForMeshSource(const MeshSource& mesh, glm::vec3 scale);

// Keeps fitted colliders matched to their part's size. A collider counts as
// fitted while it still equals colliderForMeshSource(); once someone edits
// it by hand it's left alone.
class PartColliderSync {
public:
    void update(ECS& ecs);
    void clear() { fitted_.clear(); }

private:
    std::unordered_map<EntityId, ColliderShape> fitted_;
};

} // namespace engine::core
