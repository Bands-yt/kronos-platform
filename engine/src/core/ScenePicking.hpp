#pragma once

#include <glm/glm.hpp>

#include "core/ECS.hpp"

namespace engine::core {

class MeshLibrary;

struct ScenePickResult {
    bool hit = false;
    EntityId entity = kNullEntity;
    float distance = 0.0f;
    glm::vec3 point{0.0f};
};

// Real ray-vs-AABB slab test (Kay/Kajiya), the same one pickEntity() runs
// per-entity in each mesh's local space. Exposed as a free, GPU/ECS-free
// function -- independent of MeshLibrary/ECS entirely -- specifically so
// it's unit-testable against hand-built bounds (see
// tests/test_main.cpp's testRayAabbIntersection()) without needing a
// live Vulkan device to construct a Mesh. `origin`/`end` are two points
// on the ray (not origin+direction) so callers control the ray's length
// directly; on a hit, `outT` is the intersection's position along
// origin->end, in [0,1].
[[nodiscard]] bool rayIntersectsAabb(glm::vec3 origin, glm::vec3 end, glm::vec3 boundsMin, glm::vec3 boundsMax,
                                     float& outT);

// Physics-independent ray-vs-scene picking: tests `origin`+`direction`
// against every Transform+Renderable entity's mesh-local AABB
// (Mesh::localBoundsMin/Max), transformed by the ray-vs-AABB test being
// done *in the mesh's own local space* (the ray is transformed by the
// entity's inverse model matrix, not the box by the forward one) rather
// than an approximate world-space AABB, and returns the closest hit.
//
// This is what studio::ViewportPanel's click-to-select uses. It is
// deliberately NOT built on Physics::raycast(): Studio runs no Physics
// simulation at all (see StudioApp.hpp's class comment), so its
// bring-up-scene entities have no Jolt RigidBody/collision shape for a
// real physics raycast to hit in the first place. engine_runtime, which
// DOES run Physics, uses the real Physics::raycast() instead for its
// look-to-interact trigger -- see core::ScriptWorldApi / Application.cpp.
// Two different queries for two genuinely different situations, not a
// duplicated implementation of the same one.
[[nodiscard]] ScenePickResult pickEntity(ECS& ecs, MeshLibrary& meshLibrary, glm::vec3 origin, glm::vec3 direction,
                                          float maxDistance);

} // namespace engine::core
