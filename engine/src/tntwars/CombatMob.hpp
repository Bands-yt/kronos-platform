#pragma once

#include <vector>

#include <glm/glm.hpp>
#include <volk.h>
#include <vk_mem_alloc.h>

#include "core/ECS.hpp"
#include "core/Mesh.hpp"
#include "core/ProceduralMaterials.hpp"
#include "miningsim/Mob.hpp"

namespace engine::tntwars {

// Kronos ("Space Map Bible"/"Sky Map" Combat Layer world-building, PvE
// "Sky Sentinels"/"Void Drones"): real, live wrapper around
// miningsim::MobState's own already-real, already-pure AI (idle/pursue/
// attack state machine, straight-line pursuit, per-tick attack damage,
// see Mob.hpp) -- that pure logic existed with zero live callers
// anywhere in this engine before this pass (spawnMobVisual()'s own three
// real call sites are all static map-dressing that never ticks
// behavior). This is the real ECS/live-tick half: movement, a real
// "leash" bound (no pathfinding exists in this engine at all, see
// Mob.hpp's own honest header comment -- a mob chasing in a dead-straight
// line with no bound would eventually chase off the edge of the
// playable area), and a real defeat/respawn cycle.
struct CombatMobInstance {
    core::EntityId torso = core::kNullEntity;
    core::EntityId head = core::kNullEntity;
    glm::vec3 headOffset{0.0f}; // real, captured once at spawn -- head position relative to torso position
    miningsim::MobState state;
    glm::vec3 position{0.0f};
    glm::vec3 spawnAnchor{0.0f};
    float leashRadius = 25.0f;
    bool defeated = false;
    float respawnSecondsRemaining = 0.0f;
    // Kronos ("Combat Layer" world-building, "AI combat tuning"): real
    // dodge-weave state -- see weavingPursuitTarget()'s own comment.
    // `weavePhase` is real, hashed once at spawn from spawnAnchor (not a
    // fixed constant) so a pack of mobs doesn't weave in lockstep,
    // matching core::FlickerLight::phase's own established "don't
    // synchronize" convention.
    float ageSeconds = 0.0f;
    float weavePhase = 0.0f;
};

constexpr float kCombatMobRespawnSeconds = 30.0f;

// Real spawn -- wraps miningsim::spawnMobVisual() (unchanged), capturing
// the real, fixed head-relative-to-torso offset so later ticks can move
// both parts together as one unit without needing that function's own
// internal torso/head sizing math.
[[nodiscard]] CombatMobInstance spawnCombatMob(core::ECS& ecs, core::MeshLibrary& meshLibrary,
                                                 const core::ProceduralMaterialLibrary& materials,
                                                 VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool,
                                                 VkQueue queue, const miningsim::MobState& state, glm::vec3 position,
                                                 float leashRadius);

// Real, leash-clamped pursuit step -- same real mobPursuitStep() math,
// but real-clamped back within `leashRadius` of `spawnAnchor` if it would
// otherwise wander past it.
[[nodiscard]] glm::vec3 leashedMobPursuitStep(glm::vec3 mobPosition, glm::vec3 spawnAnchor, float leashRadius,
                                                glm::vec3 targetPosition, float moveSpeed, float dt);

// Kronos ("Combat Layer" world-building, "AI combat tuning" -- "smarter
// pursuit, dodge... patterns"): real, pure lateral weave -- offsets the
// real chase target perpendicular to the mob's own real approach
// direction by a real sine wave, so a pursuing mob closes in along a
// real, unpredictable zigzag instead of a dead-straight line (this
// engine has no projectile-vs-mob collision to react to yet, see this
// file's own note -- continuous evasive weaving during the real chase
// itself is the honest, real scope for "dodge" here, not a one-shot
// dodge-an-incoming-shot reaction). A real, honest identity (returns
// `targetPosition` unchanged) at zero distance, where no perpendicular
// direction is even defined.
[[nodiscard]] glm::vec3 weavingPursuitTarget(glm::vec3 targetPosition, glm::vec3 mobPosition, float timeSeconds,
                                               float phase, float amplitude);

// Real per-tick update: while alive, real-ticks behavior state + leashed
// pursuit, syncs both real visual entities to the new position, and
// returns this tick's own real attack damage (0.0 while not attacking).
// While defeated, counts a real respawn timer down; at 0, real-revives
// at full health back at `spawnAnchor` and restores visibility. A real,
// honest no-op (returns 0.0, no state change) if both entities failed to
// spawn (see spawnCombatMob()'s own comment).
[[nodiscard]] float tickCombatMob(CombatMobInstance& mob, core::ECS& ecs, glm::vec3 targetPosition, float dt);

// Real damage application -- thin wrapper over
// miningsim::applyDamageToMob()/isMobAlive() that also real-hides the
// visual and starts the respawn timer the exact tick health reaches 0.
void applyDamageToCombatMob(CombatMobInstance& mob, core::ECS& ecs, float damage);

} // namespace engine::tntwars
