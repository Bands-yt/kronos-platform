#pragma once

#include <random>
#include <vector>

#include <glm/glm.hpp>

#include "core/ECS.hpp"

namespace engine::core {

// Configuration for one emitter -- attached to an entity via the
// ParticleEmitter component below; particles spawn at that entity's
// current Transform::position each tick.
struct ParticleEmitterSettings {
    bool enabled = true;
    // true: continuous emission at `emissionRate` particles/second.
    // false: one-shot burst -- `emissionRate` is read as a particle
    // *count* instead of a rate, spawned once, then `enabled` is set
    // false automatically (see ParticleSystem::update()). Covers the two
    // common cases (a steady effect like smoke, a one-off burst like an
    // explosion) without a separate "trigger" API this pass doesn't need.
    bool looping = true;
    float emissionRate = 20.0f;
    float particleLifetime = 1.5f;
    float particleLifetimeVariance = 0.3f; // +/- seconds, uniform random
    glm::vec3 velocityMin{-0.5f, 2.0f, -0.5f};
    glm::vec3 velocityMax{0.5f, 4.0f, 0.5f};
    glm::vec3 gravity{0.0f, -2.0f, 0.0f};
    float sizeStart = 0.15f;
    float sizeEnd = 0.02f;
    glm::vec4 colorStart{1.0f, 0.8f, 0.3f, 1.0f};
    glm::vec4 colorEnd{1.0f, 0.2f, 0.05f, 0.0f};
};

// ECS component -- marks an entity as a particle emitter. Real component,
// not a placeholder: ParticleSystem::update() reads every one of these
// that exists each tick.
struct ParticleEmitter {
    ParticleEmitterSettings settings;
    float emitAccumulator = 0.0f; // fractional-particle carryover -- see update()'s comment
};

// One live particle. Deliberately NOT an ECS entity -- thousands of these
// as individual entt entities would carry per-entity component-storage
// overhead for something that's really just a flat, homogeneous array
// simulated in a tight loop with no other component ever attached to it.
// ParticleSystem owns a plain pool instead.
struct Particle {
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};
    glm::vec3 gravity{0.0f};
    float age = 0.0f;
    float lifetime = 1.0f;
    float sizeStart = 0.1f;
    float sizeEnd = 0.1f;
    glm::vec4 colorStart{1.0f};
    glm::vec4 colorEnd{1.0f};

    [[nodiscard]] float normalizedAge() const { return lifetime > 0.0f ? age / lifetime : 1.0f; }
    [[nodiscard]] float currentSize() const;
    [[nodiscard]] glm::vec4 currentColor() const;
};

// CPU-driven particle simulation -- spawns from every ParticleEmitter
// component in the ECS, integrates position with simple Euler stepping
// (velocity += gravity*dt; position += velocity*dt -- no collision, no
// curl noise, no per-particle drag), and prunes expired particles.
// GPU-driven simulation (particles integrated in a compute shader, never
// round-tripping through the CPU) is the natural next step once a real
// particle *budget* exists to justify it -- explicitly out of scope here
// per "CPU-driven first, GPU later".
//
// Rendering is a separate concern (Renderer::drawParticles(), a camera-
// facing billboard quad per particle via GPU instancing) -- this class
// only owns simulation state and knows nothing about Vulkan.
class ParticleSystem {
public:
    void update(float dt, ECS& ecs);

    [[nodiscard]] const std::vector<Particle>& liveParticles() const { return particles_; }
    [[nodiscard]] size_t liveCount() const { return particles_.size(); }

    // Generous headroom, not a tuned content budget -- see
    // Renderer::kMaxInstancesPerFrame's declaration comment for the same
    // caveat applied to object instancing.
    static constexpr size_t kMaxParticles = 8192;

private:
    void spawnParticle(const ParticleEmitterSettings& settings, glm::vec3 origin);

    std::vector<Particle> particles_;
    std::mt19937 rng_{2024}; // fixed seed -- reproducible particle behavior run to run, not true randomness
};

} // namespace engine::core
