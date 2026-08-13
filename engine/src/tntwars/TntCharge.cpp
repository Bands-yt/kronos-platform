#include "tntwars/TntCharge.hpp"

#include <algorithm>

namespace engine::tntwars {

namespace {
float falloff(glm::vec3 center, glm::vec3 target, float radius) {
    if (radius <= 0.0f) return 0.0f;
    float distance = glm::length(target - center);
    if (distance >= radius) return 0.0f;
    return 1.0f - (distance / radius);
}
} // namespace

TntChargeState placeTntCharge(net::PlayerId owner, glm::vec3 position, float fuseSeconds, float explosionRadius,
                               float explosionMaxDamage, float explosionMaxImpulse) {
    TntChargeState state;
    state.owner = owner;
    state.position = position;
    state.fuseSecondsRemaining = fuseSeconds;
    state.explosionRadius = explosionRadius;
    state.explosionMaxDamage = explosionMaxDamage;
    state.explosionMaxImpulse = explosionMaxImpulse;
    return state;
}

void tickTntCharge(TntChargeState& state, float dt) {
    if (state.detonated || dt <= 0.0f) return;
    state.fuseSecondsRemaining -= dt;
    if (state.fuseSecondsRemaining <= 0.0f) {
        state.fuseSecondsRemaining = 0.0f;
        state.detonated = true;
    }
}

float computeExplosionDamage(glm::vec3 explosionCenter, glm::vec3 targetPosition, float radius, float maxDamage) {
    return falloff(explosionCenter, targetPosition, radius) * maxDamage;
}

ExplosionImpulse computeExplosionImpulse(glm::vec3 explosionCenter, glm::vec3 targetPosition, float radius,
                                          float maxImpulse) {
    ExplosionImpulse impulse;
    glm::vec3 delta = targetPosition - explosionCenter;
    float distance = glm::length(delta);
    impulse.direction = distance < 1e-4f ? glm::vec3(0.0f, 1.0f, 0.0f) : delta / distance;
    impulse.magnitude = falloff(explosionCenter, targetPosition, radius) * maxImpulse;
    return impulse;
}

void applyExplosionToSegments(std::vector<DestructibleSegment>& segments, glm::vec3 explosionCenter, float radius,
                               float maxDamage) {
    for (DestructibleSegment& segment : segments) {
        applyDamageToSegment(segment, computeExplosionDamage(explosionCenter, segment.position, radius, maxDamage));
    }
}

} // namespace engine::tntwars
