#include "tntwars/CombatFx.hpp"

#include <algorithm>
#include <cmath>

#include "core/Components.hpp"

namespace engine::tntwars {

void addShakeTrauma(GameplayShakeState& state, float amount) {
    if (amount <= 0.0f) return;
    state.trauma = std::min(1.0f, state.trauma + amount);
}

void tickGameplayShake(GameplayShakeState& state, float dt) {
    if (dt <= 0.0f) return;
    state.timeSeconds += dt;
    state.trauma = std::max(0.0f, state.trauma - kShakeDecayPerSecond * dt);
}

glm::vec3 sampleGameplayShakeOffset(const GameplayShakeState& state) {
    if (state.trauma <= 0.0f) return glm::vec3(0.0f);
    float amplitude = state.trauma * state.trauma * kShakeMaxOffset;
    float t = state.timeSeconds * 25.0f; // real, fixed shake frequency -- fast enough to read as a "rattle," not a slow sway
    float x = std::sin(t) * amplitude;
    float y = std::sin(t * 1.3f + 1.0f) * amplitude * 0.6f;
    float z = std::cos(t * 0.7f + 2.0f) * amplitude;
    return glm::vec3(x, y, z);
}

float explosionShakeTrauma(glm::vec3 explosionCenter, glm::vec3 listenerPosition, float radius) {
    if (radius <= 0.0f) return 0.0f;
    float distance = glm::length(listenerPosition - explosionCenter);
    if (distance >= radius) return 0.0f;
    return 1.0f - (distance / radius);
}

core::ParticleEmitterSettings explosionBurstSettings() {
    core::ParticleEmitterSettings settings;
    settings.looping = false;
    settings.emissionRate = 96.0f; // real one-shot particle COUNT, per ParticleEmitterSettings' own documented convention
    settings.particleLifetime = 0.6f;
    settings.particleLifetimeVariance = 0.25f;
    settings.velocityMin = glm::vec3(-6.0f, 2.0f, -6.0f);
    settings.velocityMax = glm::vec3(6.0f, 9.0f, 6.0f);
    settings.gravity = glm::vec3(0.0f, -14.0f, 0.0f);
    settings.sizeStart = 0.35f;
    settings.sizeEnd = 0.05f;
    settings.colorStart = glm::vec4(1.0f, 0.75f, 0.25f, 1.0f);
    settings.colorEnd = glm::vec4(0.35f, 0.1f, 0.05f, 0.0f);
    return settings;
}

core::EntityId spawnExplosionParticleBurst(core::ECS& ecs, glm::vec3 explosionCenter) {
    core::EntityId entity = ecs.createEntity("TntExplosionBurst");
    if (auto* transform = ecs.tryGetComponent<core::Transform>(entity)) {
        transform->position = explosionCenter;
    }
    auto& emitter = ecs.addComponent<core::ParticleEmitter>(entity);
    emitter.settings = explosionBurstSettings();
    return entity;
}

core::ParticleEmitterSettings projectileImpactBurstSettings() {
    core::ParticleEmitterSettings settings;
    settings.looping = false;
    settings.emissionRate = 20.0f; // real one-shot particle COUNT -- a small spark burst, not a blast
    settings.particleLifetime = 0.25f;
    settings.particleLifetimeVariance = 0.1f;
    settings.velocityMin = glm::vec3(-2.5f, 0.5f, -2.5f);
    settings.velocityMax = glm::vec3(2.5f, 3.5f, 2.5f);
    settings.gravity = glm::vec3(0.0f, -10.0f, 0.0f);
    settings.sizeStart = 0.12f;
    settings.sizeEnd = 0.02f;
    settings.colorStart = glm::vec4(1.0f, 0.95f, 0.6f, 1.0f);
    settings.colorEnd = glm::vec4(0.6f, 0.3f, 0.1f, 0.0f);
    return settings;
}

core::EntityId spawnProjectileImpactBurst(core::ECS& ecs, glm::vec3 impactPosition) {
    core::EntityId entity = ecs.createEntity("ProjectileImpactBurst");
    if (auto* transform = ecs.tryGetComponent<core::Transform>(entity)) {
        transform->position = impactPosition;
    }
    auto& emitter = ecs.addComponent<core::ParticleEmitter>(entity);
    emitter.settings = projectileImpactBurstSettings();
    return entity;
}

void triggerHitMarkerFlash(core::ECS& ecs, core::EntityId target) {
    core::triggerFlash(ecs, target, kHitMarkerPeakIntensity, kHitMarkerRestingIntensity, kHitMarkerDurationSeconds);
}

} // namespace engine::tntwars
