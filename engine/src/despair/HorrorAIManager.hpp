#pragma once

#include <glm/glm.hpp>

#include "core/ECS.hpp"
#include "core/Physics.hpp"
#include "despair/SanitySystem.hpp"

namespace engine::despair {

// Both AI creatures and the player are authored with Transform::position at
// their feet (floor level). HorrorAIManager's line-of-sight raycasts lift
// both endpoints by this height to clear the floor collider; FPSPlayerController
// uses the same constant for its camera height so what the AI can raycast to
// matches what the player camera actually sees from.
inline constexpr float kPlayerEyeHeight = 1.6f;

// PROJECT: DESPAIR -- the spec's "three-tier AI" (StalkerAI, TormentorAI,
// DespairCullerAI). Same honest constraint Mob.hpp already committed this
// engine to: core/Navigation.hpp is NavMarker/WorldBoundary scaffolding,
// not a pathfinding solver, and there is no pathfinding solver anywhere in
// this codebase. Every AI here moves in a real, honest straight line
// toward its current target (see each *MovementStep() below) -- never a
// fabricated navmesh path -- and "can this AI see the player" is always a
// real core::Physics::raycast() call, never a fabricated omniscient check.
//
// AI entities are plain Transform-driven, like miningsim::MobState/
// MobVisual -- no RigidBody attached to the creature itself, so a
// perception raycast cast from an AI's own position never self-hits its
// own collider (see core::Physics::isGrounded()'s "skin" comment for why
// that trap exists at all; it doesn't apply here because these entities
// carry no collider to self-hit).
//
// Each state machine below is split the same way miningsim::tickMobBehaviorState
// is: a pure `tick*State()` function taking positions and pre-resolved
// booleans (no ECS, no Physics), so the transition logic itself is
// testable with plain vec3s/bools and no live Jolt instance, plus a
// `*MovementStep()` pure function for the direct-line move. HorrorAIManager
// itself is the only piece that touches ECS/Physics -- it resolves real
// raycasts and gaze cones, then feeds the results into the pure functions.

// ---------------------------------------------------------------------
// StalkerAI ("The Parallel Stalker") -- the SanityHazard with
// gazeExponential=true that SanityHazard.hpp's own comment already
// promises this AI attaches to itself. Beyond that documented drain
// behavior, the "freezes solid the instant the player looks directly at
// it" rule below is this pass's own design choice for what makes a
// *stalker* distinct from a tormentor (a classic, real, well-understood
// horror mechanic), not text lifted from the original one-line spec.
enum class StalkerBehaviorState { Dormant, Stalking, Frozen };

struct StalkerAIState {
    StalkerBehaviorState behavior = StalkerBehaviorState::Dormant;
    float moveSpeed = 1.5f; // deliberately slower than the player -- it relies on stealth, not a footrace
    float detectionRadius = 20.0f;
    glm::vec3 lastKnownPlayerPos{0.0f};

    // Below this distance from lastKnownPlayerPos, treat "arrived" as
    // "nothing more to stalk toward" and drop back to Dormant.
    static constexpr float kArrivalEpsilon = 0.5f;
};

// Pure transition step. `hasLineOfSight`/`playerLooksAtStalker` are real
// raycast + gaze-cone results resolved one level up by HorrorAIManager;
// `playerLooksAtStalker` already implies mutual visibility (the caller
// only sets it true when the player's own forward-gaze cone AND an
// unobstructed raycast both land on this stalker).
void tickStalkerState(StalkerAIState& stalker, glm::vec3 stalkerPos, glm::vec3 playerPos, bool hasLineOfSight,
                       bool playerLooksAtStalker);

// Real, honest no-op (returns stalkerPos unchanged) while Dormant or
// Frozen -- movement only happens while Stalking, straight-line toward
// lastKnownPlayerPos, clamped so it doesn't overshoot past it.
[[nodiscard]] glm::vec3 stalkerMovementStep(const StalkerAIState& stalker, glm::vec3 stalkerPos, float dt);

// ---------------------------------------------------------------------
// TormentorAI -- the documented reader of PlayerNoiseLevel (see that
// struct's own comment in SanitySystem.hpp: "TormentorAI's
// investigate-noise behavior"). Investigates noise and half-seen
// movement; escalates to a real, direct hunt only once it actually has
// eyes on the player.
enum class TormentorBehaviorState { Idle, Investigating, Hunting };

struct TormentorAIState {
    TormentorBehaviorState behavior = TormentorBehaviorState::Idle;
    float moveSpeed = 4.0f;
    float visionRadius = 15.0f;
    // Radius at which PlayerNoiseLevel::current == 1.0 (sprinting/shouting)
    // is heard; scales linearly down with the player's actual noise level,
    // so a crouched-silent player (current == 0) is never heard at all,
    // regardless of distance -- a real, honest use of the seam
    // PlayerNoiseLevel exists for.
    float hearingRadiusAtFullNoise = 25.0f;
    glm::vec3 investigateTarget{0.0f};
    float loseInterestTimer = 0.0f;
    static constexpr float kLoseInterestSeconds = 5.0f;
};

// No facing/turning system exists for AI creatures yet (they're plain
// Transform-driven, like miningsim::MobState -- see this file's own top
// comment), so "can this Tormentor see the player" is honestly modeled as
// omni-directional: unobstructed `hasLineOfSight` within `visionRadius`,
// not a facing-dependent cone. A real vision cone would need a real
// facing direction to test against, which nothing here maintains.
void tickTormentorState(TormentorAIState& tormentor, float dt, glm::vec3 tormentorPos, glm::vec3 playerPos,
                         float playerNoiseLevel, bool hasLineOfSight);

[[nodiscard]] glm::vec3 tormentorMovementStep(const TormentorAIState& tormentor, glm::vec3 tormentorPos, float dt);

// ---------------------------------------------------------------------
// DespairCullerAI -- the spec's last-resort threat, spawned/armed only
// once the player is actually hallucinating (sanity < SanityState::
// kHallucinationThreshold). HorrorAIManager wires this to
// SanitySystem::setOnHallucinationThresholdCrossed (see that function's
// own comment: "HorrorAIManager's future Despair Culler spawn logic hooks
// this instead of polling every tick") -- CullerBehaviorState is a real
// one-way latch, not something that polls SanityState::hallucinating
// itself. Once armed it always knows where the hunted player is (a
// deliberate, honest "no stealth left once you're this far gone" horror
// trope -- not a LOS-gated hunt like Stalker/Tormentor above) and does
// not stand down even if sanity later recovers -- a real culler, once
// summoned, stays summoned for the rest of this vertical slice.
enum class CullerBehaviorState { Dormant, Hunting };

struct DespairCullerAIState {
    CullerBehaviorState behavior = CullerBehaviorState::Dormant;
    float moveSpeed = 6.0f; // fastest of the three tiers
};

void setCullerHunting(DespairCullerAIState& culler);

[[nodiscard]] glm::vec3 cullerMovementStep(const DespairCullerAIState& culler, glm::vec3 cullerPos,
                                            glm::vec3 huntTargetPos, float dt);

// ---------------------------------------------------------------------
// Real, ECS + Physics-driven per-tick driver for all three tiers above.
// Constructed with the live SanitySystem instance so its constructor can
// register the hallucination-crossed hook Cullers need; SanitySystem
// itself is otherwise untouched (still only reads Transform/Light/
// SanityHazard -- see its own class comment).
class HorrorAIManager {
public:
    explicit HorrorAIManager(SanitySystem& sanitySystem);

    void update(float dt, core::ECS& ecs, core::Physics& physics);

private:
    // Real, honest simplification for this vertical slice: every AI picks
    // the single nearest SanityState-bearing entity as its one target,
    // rather than tracking a full per-target list -- matches the "single
    // hunted player" shape of the spec (David Miller) even though the ECS/
    // networking layer underneath can technically hold more than one such
    // entity at once.
    [[nodiscard]] static bool findNearestPlayer(core::ECS& ecs, glm::vec3 fromPos, core::EntityId& outPlayer,
                                                 glm::vec3& outPlayerPos, glm::vec3& outPlayerForward);

    // Real raycast-backed LOS: casts from `fromPos` toward `toPos` and
    // reports clear whenever nothing blocks the path before reaching
    // `toEntity` itself -- either the ray hits nothing at all, or its
    // first hit *is* `toEntity`. Anything else (a wall, a prop) that hits
    // strictly closer than `toEntity`'s own distance counts as blocked.
    [[nodiscard]] static bool hasLineOfSight(core::Physics& physics, glm::vec3 fromPos, glm::vec3 toPos,
                                              core::EntityId toEntity);

    void tickStalkers(float dt, core::ECS& ecs, core::Physics& physics);
    void tickTormentors(float dt, core::ECS& ecs, core::Physics& physics);
    void tickCullers(float dt, core::ECS& ecs);

    // Set by the SanitySystem hallucination-crossed callback registered in
    // the constructor; latched true forever the first time any tracked
    // player crosses the threshold (see CullerBehaviorState's own comment
    // on why this never resets).
    bool hallucinationActive_ = false;
    core::EntityId huntTarget_ = core::kNullEntity;
};

} // namespace engine::despair
