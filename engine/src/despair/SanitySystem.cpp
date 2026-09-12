#include "despair/SanitySystem.hpp"

#include <algorithm>
#include <cmath>

#include "core/Hierarchy.hpp"

namespace engine::despair {

float SanitySystem::computeIlluminationAt(core::ECS& ecs, const glm::vec3& worldPos) const {
    float total = 0.0f;
    auto lightView = ecs.view<core::Light>();
    for (auto entity : lightView) {
        auto& light = lightView.get<core::Light>(entity);
        if (!light.enabled) continue;

        glm::vec3 lightPos = glm::vec3(core::hierarchy::computeWorldMatrix(ecs, entity)[3]);
        float distance = glm::length(lightPos - worldPos);
        if (distance >= light.radius) continue;

        // Real inverse-square falloff, clamped at distance=0 to avoid a
        // divide blow-up for a light sitting exactly on the sample
        // point, then linearly faded to 0 right at `radius` so a light
        // doesn't cut off with a visible pop at its own boundary.
        float attenuation = 1.0f / (1.0f + distance * distance);
        float edgeFade = 1.0f - (distance / light.radius);
        total += light.intensity * attenuation * edgeFade;
    }
    return total;
}

void SanitySystem::update(float dt, core::ECS& ecs) {
    auto playerView = ecs.view<core::Transform, SanityState>();
    for (auto entity : playerView) {
        auto& state = playerView.get<SanityState>(entity);
        if (state.current <= 0.0f && state.hallucinating) {
            // Pure perf skip, not a correctness requirement: the
            // nowHallucinating && !state.hallucinating check below already
            // makes the callback fire-once-per-crossing on its own. At
            // current == 0 there's nothing left to drain either way, so
            // just skip the illumination/hazard scan.
            continue;
        }

        glm::vec3 playerPos = glm::vec3(core::hierarchy::computeWorldMatrix(ecs, entity)[3]);
        core::Transform& playerTransform = playerView.get<core::Transform>(entity);
        // -Z-at-identity, matching this engine's existing world-forward
        // convention (core::Camera::forward() resolves to (0,0,-1) at its
        // own default yaw=-90) -- see SanityHazard's header comment.
        // FPSPlayerController (not built yet) must write Transform::rotation
        // under this same convention for gaze detection to line up.
        glm::vec3 playerForward =
            glm::normalize(playerTransform.rotation * glm::vec3(0.0f, 0.0f, -1.0f));

        float drain = 0.0f;
        bool gazedAtThisTick = false;

        float illumination = computeIlluminationAt(ecs, playerPos);
        if (illumination < kDarknessThreshold) {
            drain += kDarknessDrainPerSecond;
        }

        auto hazardView = ecs.view<core::Transform, SanityHazard>();
        for (auto hazardEntity : hazardView) {
            auto& hazard = hazardView.get<SanityHazard>(hazardEntity);
            if (!hazard.enabled || hazardEntity == entity) continue;

            glm::vec3 hazardPos = glm::vec3(core::hierarchy::computeWorldMatrix(ecs, hazardEntity)[3]);
            glm::vec3 toHazard = hazardPos - playerPos;
            float distance = glm::length(toHazard);
            if (distance >= hazard.radius || distance <= 0.0001f) continue;

            drain += hazard.proximityDrainPerSecond;

            float gazeDot = glm::dot(playerForward, glm::normalize(toHazard));
            if (gazeDot >= hazard.gazeCosThreshold) {
                gazedAtThisTick = true;
                if (hazard.gazeExponential) {
                    drain += hazard.gazeDrainPerSecond *
                             std::exp(hazard.gazeExponentialRatePerSecond * state.activeGazeSeconds);
                } else {
                    drain += hazard.gazeDrainPerSecond;
                }
            }
        }

        state.activeGazeSeconds = gazedAtThisTick ? state.activeGazeSeconds + dt : 0.0f;

        state.current = std::clamp(state.current - drain * dt, 0.0f, state.max);

        bool nowHallucinating = state.current < SanityState::kHallucinationThreshold;
        if (nowHallucinating && !state.hallucinating && onHallucinationThresholdCrossed_) {
            onHallucinationThresholdCrossed_(entity, state.current);
        }
        state.hallucinating = nowHallucinating;
    }
}

void SanitySystem::applyInstantDelta(core::ECS& ecs, core::EntityId entity, float delta) {
    auto* state = ecs.tryGetComponent<SanityState>(entity);
    if (!state) return;
    state->current = std::clamp(state->current + delta, 0.0f, state->max);

    // Same edge-triggered latch update() applies -- a negative delta
    // (a scripted shock) can cross the hallucination threshold just as
    // legitimately as gradual drain, and a positive one (the Injector)
    // can lift the player back out of it, re-arming the latch for the
    // next downward crossing.
    bool nowHallucinating = state->current < SanityState::kHallucinationThreshold;
    if (nowHallucinating && !state->hallucinating && onHallucinationThresholdCrossed_) {
        onHallucinationThresholdCrossed_(entity, state->current);
    }
    state->hallucinating = nowHallucinating;
}

bool SanitySystem::isHallucinating(core::ECS& ecs, core::EntityId entity) const {
    auto* state = ecs.tryGetComponent<SanityState>(entity);
    return state != nullptr && state->hallucinating;
}

void SanitySystem::setOnHallucinationThresholdCrossed(ThresholdCallback callback) {
    onHallucinationThresholdCrossed_ = std::move(callback);
}

} // namespace engine::despair
