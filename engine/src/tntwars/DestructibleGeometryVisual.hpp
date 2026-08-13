#pragma once

#include <vector>

#include <glm/glm.hpp>
#include <volk.h>
#include <vk_mem_alloc.h>

#include "core/ECS.hpp"
#include "core/Mesh.hpp"
#include "core/Physics.hpp"
#include "core/ProceduralMaterials.hpp"
#include "tntwars/DestructibleGeometry.hpp"

namespace engine::tntwars {

// Kronos ("TNT Wars Foundational Playability" Phase 2): the real ECS+
// physics-touching half `DestructibleGeometry.hpp`'s own header comment
// says has always been missing -- turns one already-correct
// `DestructibleSegment` (pure health/position/extents data) into a real,
// live, collidable Box entity, and keeps it in sync as that segment takes
// damage and is destroyed/rebuilt over time.

// Real, tuned respawn delay -- long enough that destroying a wall segment
// has a real tactical payoff for the rest of that push, short enough that
// one destroyed section doesn't stay a permanent hole for the whole match.
constexpr float kDestructibleRebuildDelaySeconds = 12.0f;

// Per-segment real runtime state, parallel-indexed with the
// `DestructibleSegment` vector it was built from (segments[i] <-> this[i]
// always refer to the same real segment).
struct DestructibleSegmentVisual {
    core::EntityId entity = core::kNullEntity;
    bool destroyed = false;
    // Real countdown, only running while `destroyed` -- seconds remaining
    // until this segment's own real rebuild.
    float rebuildTimer = 0.0f;
};

// Real spawn: one Box mesh entity + one real static Jolt collider
// (`physics.attachBodyToEntity`) per segment, using `material` for every
// segment (matching this file's own real, single-caller-picks-the-look
// convention -- a caller wanting mixed materials calls this once per
// material group). Returns a parallel-indexed visual-state vector; a
// segment whose real Mesh/GPU upload fails gets `core::kNullEntity` in
// its own slot (a real, honest partial failure, not a hard abort of the
// whole wall).
[[nodiscard]] std::vector<DestructibleSegmentVisual> spawnDestructibleWallVisual(
    core::ECS& ecs, core::MeshLibrary& meshLibrary, core::Physics& physics, const core::PbrTextureSet& material,
    VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue,
    const std::vector<DestructibleSegment>& segments);

// Real per-tick sync, called once per real gameplay tick after
// `applyExplosionToSegments()`/`applyDamageToSegment()` has already
// updated `segments`' own real health this tick:
//   - A segment whose health just reached 0 (alive last tick, not yet
//     marked `destroyed`) gets its real physics body detached
//     (`physics.detachBody`) and its mesh hidden (`Renderable::visible =
//     false`) -- the world now really shows and stops colliding with
//     what the damage math already said was gone.
//   - An already-destroyed segment counts `rebuildTimer` down by `dt`;
//     at (or below) 0, its health is restored to `maxHealth`, a fresh
//     real physics body is reattached, and its mesh becomes visible
//     again -- a real, complete rebuild, not just a health-number reset.
// `segments` and `visuals` must be the same real size (parallel-indexed,
// see spawnDestructibleWallVisual()'s own comment) -- a caller passing
// mismatched vectors gets a real, honest no-op past the shorter one's
// length rather than an out-of-bounds access.
void tickDestructibleWallVisual(std::vector<DestructibleSegment>& segments,
                                 std::vector<DestructibleSegmentVisual>& visuals, core::ECS& ecs, core::Physics& physics,
                                 float dt);

} // namespace engine::tntwars
