#include "despair/HorrorAIManager.hpp"

#include <algorithm>
#include <limits>

#include "core/Components.hpp"
#include "core/Hierarchy.hpp"
#include "despair/InteractionSystem.hpp"

namespace engine::despair {

namespace {

// Same real, honest "move straight toward the target, clamp so it never
// overshoots" step miningsim::mobPursuitStep already established for this
// engine's real answer to "no navmesh exists." A real, honest no-op
// (returns `from` unchanged) on a non-positive dt or when already at the
// target.
glm::vec3 directLineStep(glm::vec3 from, glm::vec3 to, float speed, float dt) {
    if (dt <= 0.0f) return from;
    glm::vec3 toTarget = to - from;
    float distance = glm::length(toTarget);
    if (distance <= 0.0001f) return from;

    float step = speed * dt;
    if (step >= distance) return to;
    return from + (toTarget / distance) * step;
}

// Same half-angle-cone convention SanityHazard::gazeCosThreshold already
// documents (~31 degrees at 0.85).
constexpr float kGazeCosThreshold = 0.85f;

// A properly hidden player (see despair::HidingSpot/PlayerHidingState in
// InteractionSystem.hpp) is unseeable by LOS-gated perception -- Stalker
// and Tormentor both route through this, so hiding really does defeat
// their line-of-sight checks, not just their distance ones. Deliberately
// NOT wired into DespairCullerAI (tickCullers() never calls this): a
// Culler is the spec's "no stealth left once you're this far gone"
// last-resort threat and doesn't use LOS at all (see its own class
// comment), so hiding never helps against one.
bool isPlayerHidden(core::ECS& ecs, core::EntityId player) {
    auto* hiding = ecs.tryGetComponent<PlayerHidingState>(player);
    return hiding != nullptr && hiding->currentSpot != core::kNullEntity;
}

// The "nothing left to do" settle point every non-hunting/investigating
// path in tickTormentorState() below now shares: Idle when there's no
// patrol route to walk (the untouched, pre-patrol behavior every existing
// test's default-constructed TormentorAIState still gets), Patrolling
// otherwise. Also advances patrolIndex (looping back to 0 past the last
// waypoint) once tormentorPos has actually reached the current one, so
// this same call re-evaluated every tick is what drives the whole route --
// no separate "advance" step needed anywhere else.
void settleToIdleOrPatrol(TormentorAIState& tormentor, glm::vec3 tormentorPos) {
    if (tormentor.patrolWaypoints.empty()) {
        tormentor.behavior = TormentorBehaviorState::Idle;
        return;
    }

    tormentor.behavior = TormentorBehaviorState::Patrolling;
    glm::vec3 currentWaypoint = tormentor.patrolWaypoints[tormentor.patrolIndex];
    if (glm::length(currentWaypoint - tormentorPos) <= TormentorAIState::kPatrolArrivalEpsilon) {
        tormentor.patrolIndex = (tormentor.patrolIndex + 1) % tormentor.patrolWaypoints.size();
    }
}

// Shared write path for all three tiers' eye-glow color: looks up
// AiEyeGlowRef on the AI entity itself (attached by FacilityMapBuilder.cpp's
// spawnAiSilhouette()) and, if present, writes `color` into the eye child's
// Renderable (base + emissive, so both the unlit glint and the bloom read
// the new color) and its real core::Light. A real, honest no-op for any AI
// entity a test constructs directly with no silhouette spawned -- exactly
// how a color-only cosmetic effect should behave when the presentation
// layer it drives doesn't exist.
// AI entities carry no RigidBody of their own (see this file's own top
// comment), so directLineStep()'s straight-line move has nothing to stop
// it at a wall -- it needs an explicit check. Cast from roughly chest
// height (clears the floor collider, same convention hasLineOfSight()
// uses) toward the proposed step; a Static body hit before `to` clamps
// the move to just short of it (by kWallSkin) instead of walking through.
constexpr float kAiBodyHeight = kPlayerEyeHeight * 0.5f;
constexpr float kWallSkin = 0.2f;

glm::vec3 clampStepAgainstWalls(core::Physics& physics, core::ECS& ecs, glm::vec3 from, glm::vec3 to) {
    glm::vec3 delta = to - from;
    float distance = glm::length(delta);
    if (distance <= 0.0001f) return to;

    glm::vec3 direction = delta / distance;
    glm::vec3 rayOrigin = from + glm::vec3(0.0f, kAiBodyHeight, 0.0f);
    core::Physics::RaycastHit hit = physics.raycast(rayOrigin, direction, distance + kWallSkin);
    if (!hit.hit || hit.distance >= distance) return to; // clear, or whatever it hit is past the destination anyway

    // Only Static geometry (walls, closed/locked doors) blocks movement --
    // a Dynamic/Kinematic body or an entity with no RigidBody at all (a
    // sensor-only prop, e.g. a keycard) is never a real wall.
    auto* rigidBody = ecs.tryGetComponent<core::RigidBody>(hit.entity);
    if (rigidBody == nullptr || rigidBody->motionType != core::RigidBodyMotionType::Static) return to;

    float clampedDistance = std::max(0.0f, hit.distance - kWallSkin);
    return from + direction * clampedDistance;
}

void applyEyeGlowColor(core::ECS& ecs, core::EntityId aiEntity, glm::vec3 color) {
    auto* ref = ecs.tryGetComponent<AiEyeGlowRef>(aiEntity);
    if (ref == nullptr || ref->eyeEntity == core::kNullEntity) return;

    if (auto* renderable = ecs.tryGetComponent<core::Renderable>(ref->eyeEntity)) {
        renderable->baseColor = glm::vec4(color, 1.0f);
        renderable->emissiveColor = color;
    }
    if (auto* light = ecs.tryGetComponent<core::Light>(ref->eyeEntity)) {
        light->color = color;
    }
}

} // namespace

// ---------------------------------------------------------------------
// StalkerAI

glm::vec3 stalkerStateColor(StalkerBehaviorState state) {
    switch (state) {
        case StalkerBehaviorState::Dormant: return {0.0f, 1.0f, 0.0f};
        case StalkerBehaviorState::Stalking: return {1.0f, 0.0f, 0.0f};
        case StalkerBehaviorState::Frozen: return {1.0f, 1.0f, 0.0f};
    }
    return {1.0f, 1.0f, 1.0f};
}

void tickStalkerState(StalkerAIState& stalker, glm::vec3 stalkerPos, glm::vec3 playerPos, bool hasLineOfSight,
                       bool playerLooksAtStalker) {
    if (playerLooksAtStalker) {
        stalker.behavior = StalkerBehaviorState::Frozen;
        stalker.lastKnownPlayerPos = playerPos;
        return;
    }

    float distanceToPlayer = glm::length(playerPos - stalkerPos);
    if (hasLineOfSight && distanceToPlayer <= stalker.detectionRadius) {
        stalker.behavior = StalkerBehaviorState::Stalking;
        stalker.lastKnownPlayerPos = playerPos;
        return;
    }

    if (stalker.behavior == StalkerBehaviorState::Stalking || stalker.behavior == StalkerBehaviorState::Frozen) {
        // Lost sight (or was just released from Frozen) -- keep creeping
        // toward the last real place it saw the player until it actually
        // gets there, then give up.
        float distanceToLastKnown = glm::length(stalker.lastKnownPlayerPos - stalkerPos);
        stalker.behavior = distanceToLastKnown <= StalkerAIState::kArrivalEpsilon ? StalkerBehaviorState::Dormant
                                                                                   : StalkerBehaviorState::Stalking;
        return;
    }

    stalker.behavior = StalkerBehaviorState::Dormant;
}

glm::vec3 stalkerMovementStep(const StalkerAIState& stalker, glm::vec3 stalkerPos, float dt) {
    if (stalker.behavior != StalkerBehaviorState::Stalking) return stalkerPos;
    return directLineStep(stalkerPos, stalker.lastKnownPlayerPos, stalker.moveSpeed, dt);
}

// ---------------------------------------------------------------------
// TormentorAI

glm::vec3 tormentorStateColor(TormentorBehaviorState state) {
    switch (state) {
        case TormentorBehaviorState::Idle:
        case TormentorBehaviorState::Patrolling: return {0.0f, 1.0f, 0.0f};
        case TormentorBehaviorState::Hunting: return {1.0f, 0.0f, 0.0f};
        case TormentorBehaviorState::Investigating: return {1.0f, 1.0f, 0.0f};
    }
    return {1.0f, 1.0f, 1.0f};
}

void tickTormentorState(TormentorAIState& tormentor, float dt, glm::vec3 tormentorPos, glm::vec3 playerPos,
                         float playerNoiseLevel, bool hasLineOfSight) {
    float distanceToPlayer = glm::length(playerPos - tormentorPos);

    if (hasLineOfSight && distanceToPlayer <= tormentor.visionRadius) {
        tormentor.behavior = TormentorBehaviorState::Hunting;
        tormentor.investigateTarget = playerPos;
        tormentor.loseInterestTimer = 0.0f;
        return;
    }

    if (tormentor.behavior == TormentorBehaviorState::Hunting) {
        // Just lost direct sight -- downgrade to investigating its last
        // known position rather than snapping straight back to Idle.
        tormentor.behavior = TormentorBehaviorState::Investigating;
        tormentor.investigateTarget = playerPos;
        tormentor.loseInterestTimer = 0.0f;
        return;
    }

    float effectiveHearingRadius = tormentor.hearingRadiusAtFullNoise * glm::clamp(playerNoiseLevel, 0.0f, 1.0f);
    if (playerNoiseLevel > 0.0f && distanceToPlayer <= effectiveHearingRadius &&
        tormentor.behavior != TormentorBehaviorState::Investigating) {
        // Only snapshot the noise's origin on the transition into
        // Investigating -- a real "go check out where that sound came
        // from," not a live homing beacon. A continuously-noisy player who
        // stays in range must NOT keep re-triggering this branch every
        // tick: if it did, investigateTarget would re-snap to their exact
        // live position (Hunting with extra steps, defeating the whole
        // "silent player is never heard" stealth seam) AND
        // loseInterestTimer would keep resetting to 0, so the Tormentor
        // could never reach kLoseInterestSeconds and give up. Once already
        // Investigating, this branch falls through to the timer below
        // instead, exactly like the Hunting case above already does
        // implicitly by not re-entering its own branch.
        tormentor.investigateTarget = playerPos;
        tormentor.behavior = TormentorBehaviorState::Investigating;
        tormentor.loseInterestTimer = 0.0f;
        return;
    }

    if (tormentor.behavior == TormentorBehaviorState::Investigating) {
        tormentor.loseInterestTimer += dt;
        if (tormentor.loseInterestTimer >= TormentorAIState::kLoseInterestSeconds) {
            settleToIdleOrPatrol(tormentor, tormentorPos);
        }
        return;
    }

    settleToIdleOrPatrol(tormentor, tormentorPos);
}

glm::vec3 tormentorMovementStep(const TormentorAIState& tormentor, glm::vec3 tormentorPos, float dt) {
    if (tormentor.behavior == TormentorBehaviorState::Idle) return tormentorPos;
    if (tormentor.behavior == TormentorBehaviorState::Patrolling) {
        if (tormentor.patrolWaypoints.empty()) return tormentorPos;
        return directLineStep(tormentorPos, tormentor.patrolWaypoints[tormentor.patrolIndex], tormentor.patrolSpeed,
                               dt);
    }
    return directLineStep(tormentorPos, tormentor.investigateTarget, tormentor.moveSpeed, dt);
}

// ---------------------------------------------------------------------
// DespairCullerAI

glm::vec3 cullerStateColor(CullerBehaviorState state) {
    switch (state) {
        case CullerBehaviorState::Dormant: return {0.0f, 1.0f, 0.0f};
        case CullerBehaviorState::Hunting: return {1.0f, 0.0f, 0.0f};
    }
    return {1.0f, 1.0f, 1.0f};
}

void setCullerHunting(DespairCullerAIState& culler) { culler.behavior = CullerBehaviorState::Hunting; }

glm::vec3 cullerMovementStep(const DespairCullerAIState& culler, glm::vec3 cullerPos, glm::vec3 huntTargetPos,
                              float dt) {
    if (culler.behavior != CullerBehaviorState::Hunting) return cullerPos;
    return directLineStep(cullerPos, huntTargetPos, culler.moveSpeed, dt);
}

// ---------------------------------------------------------------------
// HorrorAIManager

HorrorAIManager::HorrorAIManager(SanitySystem& sanitySystem) {
    // The one real registered consumer SanitySystem.hpp's own comment on
    // setOnHallucinationThresholdCrossed already promised -- fires exactly
    // once per downward crossing, which is exactly the one-way latch
    // CullerBehaviorState wants (see this file's own comment on why
    // cullers never stand back down).
    sanitySystem.setOnHallucinationThresholdCrossed([this](core::EntityId entity, float /*sanity*/) {
        hallucinationActive_ = true;
        huntTarget_ = entity;
    });
}

bool HorrorAIManager::findNearestPlayer(core::ECS& ecs, glm::vec3 fromPos, core::EntityId& outPlayer,
                                         glm::vec3& outPlayerPos, glm::vec3& outPlayerForward) {
    outPlayer = core::kNullEntity;
    float nearestDistance = std::numeric_limits<float>::max();

    auto playerView = ecs.view<core::Transform, SanityState>();
    for (auto entity : playerView) {
        auto& transform = playerView.get<core::Transform>(entity);
        glm::vec3 pos = glm::vec3(core::hierarchy::computeWorldMatrix(ecs, entity)[3]);
        float distance = glm::length(pos - fromPos);
        if (distance >= nearestDistance) continue;

        nearestDistance = distance;
        outPlayer = entity;
        outPlayerPos = pos;
        // Same -Z-at-identity forward convention SanitySystem::update()
        // already uses and documents.
        outPlayerForward = glm::normalize(transform.rotation * glm::vec3(0.0f, 0.0f, -1.0f));
    }

    return outPlayer != core::kNullEntity;
}

bool HorrorAIManager::hasLineOfSight(core::Physics& physics, glm::vec3 fromPos, glm::vec3 toPos,
                                      core::EntityId toEntity) {
    // A raycast between two feet-level points runs tangent along the floor
    // collider's top face for its whole length -- lift both endpoints by
    // kPlayerEyeHeight so only real obstacles (walls, props) block it.
    glm::vec3 eyeFrom = fromPos + glm::vec3(0.0f, kPlayerEyeHeight, 0.0f);
    glm::vec3 eyeTo = toPos + glm::vec3(0.0f, kPlayerEyeHeight, 0.0f);
    glm::vec3 toTarget = eyeTo - eyeFrom;
    float distance = glm::length(toTarget);
    if (distance <= 0.0001f) return true;

    auto hit = physics.raycast(eyeFrom, toTarget / distance, distance);
    if (!hit.hit) return true; // nothing at all in the way
    if (hit.entity == toEntity) return true; // first thing hit is the target itself
    return hit.distance >= distance; // something else, but no closer than the target
}

void HorrorAIManager::tickStalkers(float dt, core::ECS& ecs, core::Physics& physics) {
    auto view = ecs.view<core::Transform, StalkerAIState>();
    for (auto entity : view) {
        auto& transform = view.get<core::Transform>(entity);
        auto& stalker = view.get<StalkerAIState>(entity);
        glm::vec3 stalkerPos = glm::vec3(core::hierarchy::computeWorldMatrix(ecs, entity)[3]);

        core::EntityId player;
        glm::vec3 playerPos, playerForward;
        if (!findNearestPlayer(ecs, stalkerPos, player, playerPos, playerForward)) {
            stalker.behavior = StalkerBehaviorState::Dormant;
            applyEyeGlowColor(ecs, entity, stalkerStateColor(stalker.behavior));
            continue;
        }

        bool losClear = !isPlayerHidden(ecs, player) && hasLineOfSight(physics, stalkerPos, playerPos, player);
        float gazeDot = glm::dot(playerForward, glm::normalize(stalkerPos - playerPos));
        bool playerLooksAtStalker = losClear && gazeDot >= kGazeCosThreshold;

        tickStalkerState(stalker, stalkerPos, playerPos, losClear, playerLooksAtStalker);
        glm::vec3 proposedPos = stalkerMovementStep(stalker, stalkerPos, dt);
        transform.position = clampStepAgainstWalls(physics, ecs, stalkerPos, proposedPos);
        applyEyeGlowColor(ecs, entity, stalkerStateColor(stalker.behavior));
    }
}

void HorrorAIManager::tickTormentors(float dt, core::ECS& ecs, core::Physics& physics) {
    auto view = ecs.view<core::Transform, TormentorAIState>();
    for (auto entity : view) {
        auto& transform = view.get<core::Transform>(entity);
        auto& tormentor = view.get<TormentorAIState>(entity);
        glm::vec3 tormentorPos = glm::vec3(core::hierarchy::computeWorldMatrix(ecs, entity)[3]);

        core::EntityId player;
        glm::vec3 playerPos, playerForward;
        if (!findNearestPlayer(ecs, tormentorPos, player, playerPos, playerForward)) {
            // No live player entity to hunt/investigate/hear at all (not
            // even out of range) -- same settle point as the pure
            // tickTormentorState()'s own fallthrough, so a Tormentor with a
            // real patrol route keeps walking it instead of freezing solid
            // just because nothing else in this frame's ECS view qualifies.
            settleToIdleOrPatrol(tormentor, tormentorPos);
            glm::vec3 proposedPos = tormentorMovementStep(tormentor, tormentorPos, dt);
            transform.position = clampStepAgainstWalls(physics, ecs, tormentorPos, proposedPos);
            applyEyeGlowColor(ecs, entity, tormentorStateColor(tormentor.behavior));
            continue;
        }

        bool losClear = !isPlayerHidden(ecs, player) && hasLineOfSight(physics, tormentorPos, playerPos, player);
        float playerNoiseLevel = 0.0f;
        if (auto* noise = ecs.tryGetComponent<PlayerNoiseLevel>(player)) {
            playerNoiseLevel = noise->current;
        }

        tickTormentorState(tormentor, dt, tormentorPos, playerPos, playerNoiseLevel, losClear);
        glm::vec3 proposedPos = tormentorMovementStep(tormentor, tormentorPos, dt);
        transform.position = clampStepAgainstWalls(physics, ecs, tormentorPos, proposedPos);
        applyEyeGlowColor(ecs, entity, tormentorStateColor(tormentor.behavior));
    }
}

void HorrorAIManager::tickCullers(float dt, core::ECS& ecs, core::Physics& physics) {
    if (!hallucinationActive_) return;

    auto* huntTargetTransform = ecs.tryGetComponent<core::Transform>(huntTarget_);
    if (!huntTargetTransform) return; // real, honest no-op if the hunted entity no longer exists
    glm::vec3 huntTargetPos = glm::vec3(core::hierarchy::computeWorldMatrix(ecs, huntTarget_)[3]);

    auto view = ecs.view<core::Transform, DespairCullerAIState>();
    for (auto entity : view) {
        auto& transform = view.get<core::Transform>(entity);
        auto& culler = view.get<DespairCullerAIState>(entity);
        glm::vec3 cullerPos = glm::vec3(core::hierarchy::computeWorldMatrix(ecs, entity)[3]);

        setCullerHunting(culler);
        glm::vec3 proposedPos = cullerMovementStep(culler, cullerPos, huntTargetPos, dt);
        transform.position = clampStepAgainstWalls(physics, ecs, cullerPos, proposedPos);
        applyEyeGlowColor(ecs, entity, cullerStateColor(culler.behavior));
    }
}

void HorrorAIManager::update(float dt, core::ECS& ecs, core::Physics& physics) {
    tickStalkers(dt, ecs, physics);
    tickTormentors(dt, ecs, physics);
    tickCullers(dt, ecs, physics);
}

} // namespace engine::despair
