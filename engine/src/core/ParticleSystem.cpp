#include "core/ParticleSystem.hpp"

#include <algorithm>

#include "core/Components.hpp"

namespace engine::core {

float Particle::currentSize() const {
    return sizeStart + (sizeEnd - sizeStart) * std::clamp(normalizedAge(), 0.0f, 1.0f);
}

glm::vec4 Particle::currentColor() const {
    return glm::mix(colorStart, colorEnd, std::clamp(normalizedAge(), 0.0f, 1.0f));
}

void ParticleSystem::spawnParticle(const ParticleEmitterSettings& settings, glm::vec3 origin) {
    if (particles_.size() >= kMaxParticles) return;

    std::uniform_real_distribution<float> vx(settings.velocityMin.x, settings.velocityMax.x);
    std::uniform_real_distribution<float> vy(settings.velocityMin.y, settings.velocityMax.y);
    std::uniform_real_distribution<float> vz(settings.velocityMin.z, settings.velocityMax.z);
    std::uniform_real_distribution<float> lifeVar(-settings.particleLifetimeVariance, settings.particleLifetimeVariance);

    Particle p;
    p.position = origin;
    p.velocity = {vx(rng_), vy(rng_), vz(rng_)};
    p.gravity = settings.gravity;
    p.lifetime = std::max(0.05f, settings.particleLifetime + lifeVar(rng_));
    p.sizeStart = settings.sizeStart;
    p.sizeEnd = settings.sizeEnd;
    p.colorStart = settings.colorStart;
    p.colorEnd = settings.colorEnd;
    particles_.push_back(p);
}

void ParticleSystem::update(float dt, ECS& ecs) {
    // Age and integrate existing particles first, pruning expired ones,
    // before spawning new ones -- so a particle spawned this tick always
    // gets a full tick's simulation next update(), not this one.
    for (Particle& p : particles_) {
        p.velocity += p.gravity * dt;
        p.position += p.velocity * dt;
        p.age += dt;
    }
    particles_.erase(
        std::remove_if(particles_.begin(), particles_.end(), [](const Particle& p) { return p.age >= p.lifetime; }),
        particles_.end());

    for (auto entity : ecs.view<Transform, ParticleEmitter>()) {
        auto* emitter = ecs.tryGetComponent<ParticleEmitter>(entity);
        auto* transform = ecs.tryGetComponent<Transform>(entity);
        if (emitter == nullptr || transform == nullptr || !emitter->settings.enabled) continue;

        if (!emitter->settings.looping) {
            int burstCount = static_cast<int>(emitter->settings.emissionRate);
            for (int i = 0; i < burstCount; ++i) {
                spawnParticle(emitter->settings, transform->position);
            }
            emitter->settings.enabled = false; // one-shot -- see ParticleEmitterSettings::looping's comment
            continue;
        }

        // Fractional accumulator: a rate like 20/s spawns roughly one
        // particle every 50ms regardless of actual tick rate, instead of
        // silently rounding emissionRate down to a multiple of the tick
        // rate (the bug a naive "if (tickCount % X == 0) spawn()" has).
        emitter->emitAccumulator += emitter->settings.emissionRate * dt;
        while (emitter->emitAccumulator >= 1.0f) {
            spawnParticle(emitter->settings, transform->position);
            emitter->emitAccumulator -= 1.0f;
        }
    }
}

} // namespace engine::core
