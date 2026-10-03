#include "brokenbones/Effects.hpp"

#include <algorithm>
#include <cmath>

#include "core/Components.hpp"

namespace engine::brokenbones {

namespace {

float strength(float speed, float low, float high) { return std::clamp((speed - low) / (high - low), 0.0f, 1.0f); }

core::ParticleEmitterSettings oneShot(float count) {
    core::ParticleEmitterSettings s;
    s.looping = false;
    s.emissionRate = std::round(count);
    return s;
}

} // namespace

core::ParticleEmitterSettings impactDust(glm::vec3 rockColor, float impactSpeed) {
    float k = strength(impactSpeed, 4.0f, 60.0f);
    core::ParticleEmitterSettings s = oneShot(14.0f + 70.0f * k);
    float spread = 1.5f + 5.0f * k;
    s.particleLifetime = 0.9f + 0.8f * k;
    s.particleLifetimeVariance = 0.35f;
    s.velocityMin = glm::vec3(-spread, 0.3f, -spread);
    s.velocityMax = glm::vec3(spread, 1.0f + 4.0f * k, spread);
    s.gravity = glm::vec3(0.0f, -1.5f, 0.0f);
    s.sizeStart = 0.25f + 0.25f * k;
    s.sizeEnd = 0.7f + 0.9f * k;
    glm::vec3 dust = glm::mix(rockColor, glm::vec3(0.75f, 0.7f, 0.62f), 0.4f);
    s.colorStart = glm::vec4(dust, 0.55f);
    s.colorEnd = glm::vec4(dust, 0.0f);
    s.occlusion = 1.0f;
    return s;
}

core::ParticleEmitterSettings boneChips(int bones) {
    core::ParticleEmitterSettings s = oneShot(static_cast<float>(std::clamp(6 + 3 * bones, 6, 60)));
    s.particleLifetime = 0.8f;
    s.particleLifetimeVariance = 0.3f;
    s.velocityMin = glm::vec3(-5.0f, 1.0f, -5.0f);
    s.velocityMax = glm::vec3(5.0f, 7.0f, 5.0f);
    s.gravity = glm::vec3(0.0f, -14.0f, 0.0f);
    s.sizeStart = 0.07f;
    s.sizeEnd = 0.05f;
    s.colorStart = glm::vec4(0.95f, 0.92f, 0.82f, 1.0f);
    s.colorEnd = glm::vec4(0.85f, 0.8f, 0.7f, 0.0f);
    s.occlusion = 1.0f;
    return s;
}

core::ParticleEmitterSettings bloodBurst(float impactSpeed) {
    float k = strength(impactSpeed, 10.0f, 70.0f);
    core::ParticleEmitterSettings s = oneShot(30.0f + 90.0f * k);
    float spread = 2.5f + 5.0f * k;
    s.particleLifetime = 0.9f;
    s.particleLifetimeVariance = 0.3f;
    s.velocityMin = glm::vec3(-spread, 0.5f, -spread);
    s.velocityMax = glm::vec3(spread, 3.0f + 5.0f * k, spread);
    s.gravity = glm::vec3(0.0f, -12.0f, 0.0f);
    s.sizeStart = 0.12f + 0.06f * k;
    s.sizeEnd = 0.05f;
    s.colorStart = glm::vec4(0.55f, 0.02f, 0.02f, 1.0f);
    s.colorEnd = glm::vec4(0.3f, 0.0f, 0.0f, 0.0f);
    s.occlusion = 1.0f;
    return s;
}

core::ParticleEmitterSettings splashColumn(float entrySpeed, bool lava) {
    float k = strength(entrySpeed, 10.0f, 120.0f);
    core::ParticleEmitterSettings s = oneShot(260.0f + 900.0f * k);
    float spread = 1.2f + 2.5f * k;
    s.particleLifetime = 1.4f + 1.2f * k;
    s.particleLifetimeVariance = 0.5f;
    s.velocityMin = glm::vec3(-spread, 6.0f, -spread);
    s.velocityMax = glm::vec3(spread, 12.0f + 30.0f * k, spread);
    s.gravity = glm::vec3(0.0f, -9.8f, 0.0f);
    s.sizeStart = 0.35f + 0.3f * k;
    s.sizeEnd = 0.2f;
    if (lava) {
        s.colorStart = glm::vec4(3.0f, 1.2f, 0.25f, 1.0f);
        s.colorEnd = glm::vec4(0.6f, 0.1f, 0.02f, 0.0f);
        s.occlusion = 0.0f;
    } else {
        s.colorStart = glm::vec4(0.92f, 0.96f, 1.0f, 0.9f);
        s.colorEnd = glm::vec4(0.8f, 0.88f, 0.95f, 0.0f);
        s.occlusion = 0.85f;
    }
    return s;
}

core::ParticleEmitterSettings splashRing(float entrySpeed, bool lava) {
    float k = strength(entrySpeed, 10.0f, 120.0f);
    core::ParticleEmitterSettings s = oneShot(200.0f + 500.0f * k);
    float spread = 5.0f + 14.0f * k;
    s.particleLifetime = 1.0f + 0.6f * k;
    s.particleLifetimeVariance = 0.3f;
    s.velocityMin = glm::vec3(-spread, 1.5f, -spread);
    s.velocityMax = glm::vec3(spread, 4.0f + 6.0f * k, spread);
    s.gravity = glm::vec3(0.0f, -9.8f, 0.0f);
    s.sizeStart = 0.22f;
    s.sizeEnd = 0.12f;
    s.colorStart = lava ? glm::vec4(2.5f, 0.9f, 0.2f, 1.0f) : glm::vec4(0.9f, 0.95f, 1.0f, 0.85f);
    s.colorEnd = lava ? glm::vec4(0.4f, 0.05f, 0.0f, 0.0f) : glm::vec4(0.85f, 0.9f, 0.95f, 0.0f);
    s.occlusion = lava ? 0.0f : 0.8f;
    return s;
}

core::ParticleEmitterSettings splashMist(float entrySpeed, bool lava) {
    float k = strength(entrySpeed, 10.0f, 120.0f);
    core::ParticleEmitterSettings s = oneShot(40.0f + 80.0f * k);
    float spread = 2.0f + 5.0f * k;
    s.particleLifetime = 3.0f + 2.0f * k;
    s.particleLifetimeVariance = 0.8f;
    s.velocityMin = glm::vec3(-spread, 1.0f, -spread);
    s.velocityMax = glm::vec3(spread, 4.0f + 8.0f * k, spread);
    s.gravity = glm::vec3(0.0f, lava ? 0.6f : -0.3f, 0.0f);
    s.sizeStart = 1.0f + 0.8f * k;
    s.sizeEnd = 3.0f + 3.0f * k;
    s.colorStart = lava ? glm::vec4(0.25f, 0.22f, 0.2f, 0.6f) : glm::vec4(0.9f, 0.94f, 0.98f, 0.35f);
    s.colorEnd = lava ? glm::vec4(0.15f, 0.14f, 0.13f, 0.0f) : glm::vec4(0.9f, 0.94f, 0.98f, 0.0f);
    s.occlusion = 1.0f;
    return s;
}

core::ParticleEmitterSettings explosionSmoke(float power) {
    core::ParticleEmitterSettings s = oneShot(50.0f + 40.0f * std::clamp(power - 1.0f, 0.0f, 2.0f));
    s.particleLifetime = 2.6f;
    s.particleLifetimeVariance = 0.7f;
    s.velocityMin = glm::vec3(-4.0f, -1.0f, -4.0f);
    s.velocityMax = glm::vec3(4.0f, 4.0f, 4.0f);
    s.gravity = glm::vec3(0.0f, 1.2f, 0.0f);
    s.sizeStart = 0.9f;
    s.sizeEnd = 3.2f;
    s.colorStart = glm::vec4(0.16f, 0.15f, 0.14f, 0.75f);
    s.colorEnd = glm::vec4(0.3f, 0.29f, 0.28f, 0.0f);
    s.occlusion = 1.0f;
    return s;
}

core::ParticleEmitterSettings bloodDrip() {
    core::ParticleEmitterSettings s;
    s.enabled = false;
    s.emissionRate = 45.0f;
    s.particleLifetime = 1.2f;
    s.particleLifetimeVariance = 0.3f;
    s.velocityMin = glm::vec3(-0.8f, -0.5f, -0.8f);
    s.velocityMax = glm::vec3(0.8f, 1.2f, 0.8f);
    s.gravity = glm::vec3(0.0f, -9.8f, 0.0f);
    s.sizeStart = 0.07f;
    s.sizeEnd = 0.04f;
    s.colorStart = glm::vec4(0.5f, 0.02f, 0.02f, 1.0f);
    s.colorEnd = glm::vec4(0.3f, 0.0f, 0.0f, 0.0f);
    s.occlusion = 1.0f;
    return s;
}

core::ParticleEmitterSettings rocketSmoke() {
    core::ParticleEmitterSettings s;
    s.enabled = false;
    s.emissionRate = 60.0f;
    s.particleLifetime = 1.8f;
    s.particleLifetimeVariance = 0.5f;
    s.gravity = glm::vec3(0.0f, 0.8f, 0.0f);
    s.sizeStart = 0.35f;
    s.sizeEnd = 1.8f;
    s.colorStart = glm::vec4(0.6f, 0.58f, 0.55f, 0.5f);
    s.colorEnd = glm::vec4(0.7f, 0.7f, 0.7f, 0.0f);
    s.occlusion = 1.0f;
    return s;
}

void EffectsPool::burst(glm::vec3 at, const core::ParticleEmitterSettings& settings) {
    if (ecs_ == nullptr) return;
    if (live_.size() >= kMaxLive) {
        ecs_->destroyEntity(live_.front().entity);
        live_.erase(live_.begin());
    }
    core::EntityId entity = ecs_->createEntity("BrokenBonesFx");
    if (auto* transform = ecs_->tryGetComponent<core::Transform>(entity)) transform->position = at;
    auto& emitter = ecs_->addComponent<core::ParticleEmitter>(entity);
    emitter.settings = settings;
    emitter.settings.enabled = true;
    emitter.settings.looping = false;
    // A one-shot spawns on its first update; the entity only has to outlive that.
    live_.push_back({entity, 0.5f});
}

void EffectsPool::update(float dt) {
    if (ecs_ == nullptr) return;
    for (auto it = live_.begin(); it != live_.end();) {
        it->secondsLeft -= dt;
        if (it->secondsLeft <= 0.0f) {
            ecs_->destroyEntity(it->entity);
            it = live_.erase(it);
        } else {
            ++it;
        }
    }
}

void EffectsPool::clear() {
    if (ecs_ != nullptr) {
        for (const Live& live : live_) ecs_->destroyEntity(live.entity);
    }
    live_.clear();
}

} // namespace engine::brokenbones
