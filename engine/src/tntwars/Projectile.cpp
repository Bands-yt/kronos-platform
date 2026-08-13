#include "tntwars/Projectile.hpp"

namespace engine::tntwars {

namespace {
constexpr float kGravity = -9.81f;
}

ProjectileTuning projectileTuningFor(ProjectileType type) {
    switch (type) {
        case ProjectileType::Rocket:
            return ProjectileTuning{34.0f, 1.0f, 5.0f, 0.6f};
        case ProjectileType::ShieldBolt:
            return ProjectileTuning{40.0f, 0.6f, 3.0f, 0.4f};
        case ProjectileType::RepairBeam:
            return ProjectileTuning{60.0f, 0.0f, 2.0f, 0.3f}; // real, gravity-immune instant-ish beam
        case ProjectileType::RadarPing:
            return ProjectileTuning{50.0f, 0.0f, 4.0f, 0.2f}; // real, gravity-immune utility ping
        case ProjectileType::Torpedo:
            return ProjectileTuning{18.0f, 0.0f, 10.0f, 0.8f}; // real, slow, gravity-immune underwater weapon
        case ProjectileType::Missile:
            // Real, slow, gravity-immune, long-lived, and real-wide hit
            // radius -- a Missile is a real, visible, trackable threat an
            // Admin Room defender has time to spot and react to, not a
            // snap-reaction shot (see Missile.hpp's own kMissileFlightSeconds,
            // the real, separate server-owned timer this tuning's own
            // lifetimeSeconds intentionally matches for visual consistency).
            return ProjectileTuning{12.0f, 0.0f, 6.0f, 1.0f};
    }
    return ProjectileTuning{};
}

ProjectileState spawnProjectile(ProjectileType type, net::PlayerId owner, glm::vec3 origin, glm::vec3 aimDirection,
                                 float damage) {
    ProjectileState state;
    state.type = type;
    state.owner = owner;
    state.position = origin;
    state.damage = damage;
    state.stealth = type == ProjectileType::Torpedo;

    ProjectileTuning tuning = projectileTuningFor(type);
    float length = glm::length(aimDirection);
    glm::vec3 direction = length > 1e-5f ? aimDirection / length : glm::vec3(1.0f, 0.0f, 0.0f);
    state.velocity = direction * tuning.initialSpeed;
    state.lifetimeSeconds = tuning.lifetimeSeconds;
    return state;
}

void stepProjectile(ProjectileState& state, float dt) {
    if (state.expired || dt <= 0.0f) return;

    ProjectileTuning tuning = projectileTuningFor(state.type);
    state.velocity.y += kGravity * tuning.gravityScale * dt;
    state.position += state.velocity * dt;

    state.lifetimeSeconds -= dt;
    if (state.lifetimeSeconds <= 0.0f) state.expired = true;
}

bool projectileHitsTarget(const ProjectileState& state, glm::vec3 targetPosition, float targetRadius) {
    if (state.expired) return false;
    ProjectileTuning tuning = projectileTuningFor(state.type);
    glm::vec3 delta = targetPosition - state.position;
    float combinedRadius = tuning.radius + targetRadius;
    return glm::dot(delta, delta) <= combinedRadius * combinedRadius;
}

} // namespace engine::tntwars
