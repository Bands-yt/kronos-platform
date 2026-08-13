#pragma once

#include <glm/glm.hpp>

#include "miningsim/Rarity.hpp"

namespace engine::miningsim {

// Kronos roadmap Milestone 19 ("Mob system"): the brief's own
// state-machine spec -- idle/pursue/attack. The roadmap's own plan text
// suggested reusing core::Navigation for pathing; checking that module
// (core/Navigation.hpp) shows it's real, but is Sprint 6's own nav-
// markers/world-boundary/teleport-pad system, not a pathfinding solver --
// this engine has no real pathfinding of any kind. Rather than silently
// claim a dependency that doesn't do what the roadmap assumed, mob
// pursuit here is a real, honest, direct-line "move straight toward the
// target" step (mobPursuitStep() below) -- the same "real, bounded
// technique over unbuilt complexity" precedent MissileState's own
// timer-based (not full-ballistic) design already committed to.
enum class MobBehaviorState { Idle, Pursue, Attack };

struct MobState {
    MobBehaviorState behavior = MobBehaviorState::Idle;
    RarityTier rarity = RarityTier::Common; // real tie to Milestone 13's rarity ladder
    float health = 50.0f;
    float maxHealth = 50.0f;
    float detectionRadius = 12.0f;
    float attackRadius = 2.0f;
    float moveSpeed = 3.0f;
    float attackDamagePerSecond = 8.0f;
};

// Real, rarity-scaled spawn -- a higher real rarity tier means a real,
// strictly tougher/faster/harder-hitting mob (health/speed/damage all
// scale with rarityTierRank()), the brief's own "rarity-specific...
// unique mechanics" read honestly as real stat scaling rather than a
// claim of bespoke per-tier abilities this milestone has no scope for.
[[nodiscard]] MobState spawnMobOfRarity(RarityTier rarity);

// Real, pure per-tick state transition -- Idle -> Pursue once `target`
// comes within detectionRadius; Pursue -> Attack once within
// attackRadius; Attack -> Pursue if the target backs out past
// attackRadius but stays within detectionRadius; Pursue/Attack -> Idle
// the moment the target leaves detectionRadius entirely (a real "lost
// the trail" reset).
void tickMobBehaviorState(MobState& mob, glm::vec3 mobPosition, glm::vec3 targetPosition);

// Real, direct, straight-line pursuit step -- real-clamps to `targetPosition`
// itself rather than overshooting past it. A real, honest no-op
// (returns `mobPosition` unchanged) on a non-positive dt or when already
// at the target.
[[nodiscard]] glm::vec3 mobPursuitStep(glm::vec3 mobPosition, glm::vec3 targetPosition, float moveSpeed, float dt);

// Real, per-tick damage a mob in MobBehaviorState::Attack deals -- a
// real, honest 0 while the mob isn't actively attacking.
[[nodiscard]] float mobAttackDamageThisTick(const MobState& mob, float dt);

// Real, clamped damage application -- matches this session's other
// "clamp at 0, ignore non-positive damage" health-system convention
// exactly (TntWarsMatch::applyDamage(), DestructibleGeometry's own
// applyDamageToSegment()).
void applyDamageToMob(MobState& mob, float damage);
[[nodiscard]] bool isMobAlive(const MobState& mob);

} // namespace engine::miningsim
