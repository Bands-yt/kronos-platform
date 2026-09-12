#include "despair/HorrorAIManager.hpp"

#include <limits>

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

} // namespace

// ---------------------------------------------------------------------
// StalkerAI

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
            tormentor.behavior = TormentorBehaviorState::Idle;
        }
        return;
    }

    tormentor.behavior = TormentorBehaviorState::Idle;
}

glm::vec3 tormentorMovementStep(const TormentorAIState& tormentor, glm::vec3 tormentorPos, float dt) {
    if (tormentor.behavior == TormentorBehaviorState::Idle) return tormentorPos;
    return directLineStep(tormentorPos, tormentor.investigateTarget, tormentor.moveSpeed, dt);
}

// ---------------------------------------------------------------------
// DespairCullerAI

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
            continue;
        }

        bool losClear = !isPlayerHidden(ecs, player) && hasLineOfSight(physics, stalkerPos, playerPos, player);
        float gazeDot = glm::dot(playerForward, glm::normalize(stalkerPos - playerPos));
        bool playerLooksAtStalker = losClear && gazeDot >= kGazeCosThreshold;

        tickStalkerState(stalker, stalkerPos, playerPos, losClear, playerLooksAtStalker);
        transform.position = stalkerMovementStep(stalker, stalkerPos, dt);
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
            tormentor.behavior = TormentorBehaviorState::Idle;
            continue;
        }

        bool losClear = !isPlayerHidden(ecs, player) && hasLineOfSight(physics, tormentorPos, playerPos, player);
        float playerNoiseLevel = 0.0f;
        if (auto* noise = ecs.tryGetComponent<PlayerNoiseLevel>(player)) {
            playerNoiseLevel = noise->current;
        }

        tickTormentorState(tormentor, dt, tormentorPos, playerPos, playerNoiseLevel, losClear);
        transform.position = tormentorMovementStep(tormentor, tormentorPos, dt);
    }
}

void HorrorAIManager::tickCullers(float dt, core::ECS& ecs) {
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
        transform.position = cullerMovementStep(culler, cullerPos, huntTargetPos, dt);
    }
}

void HorrorAIManager::update(float dt, core::ECS& ecs, core::Physics& physics) {
    tickStalkers(dt, ecs, physics);
    tickTormentors(dt, ecs, physics);
    tickCullers(dt, ecs);
}

} // namespace engine::despair
