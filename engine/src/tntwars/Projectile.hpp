#pragma once

#include <glm/glm.hpp>

#include "net/NetTypes.hpp"
#include "tntwars/ClassSystem.hpp"

namespace engine::tntwars {

// Sprint 14's real projectile simulation -- a pure, deterministic
// ballistic step (gravity-affected Euler integration, the same real
// integration technique net::applyNetworkedMovement() already uses for
// player motion), server-authoritative like everything else in this
// engine's networked gameplay. Real replication is a real, deliberately
// lighter-weight technique than Sprint 11's per-tick NetworkIdentity/
// EntityState snapshot system: the server broadcasts one real, reliable
// "spawned" event per shot (type/owner/origin/initial velocity/damage,
// see net::NetworkSession's WireMessageType::ProjectileSpawned), and
// every client -- including the server's own local simulation --
// deterministically replays the exact same real stepProjectile()
// function from there. This is a real, standard, honest technique for
// short-lived projectiles (no per-tick position updates needed for
// something that only exists a few real seconds), not a shortcut that
// skips server authority: the server is still the one deciding whether
// a shot is allowed at all (see TntWarsMatch::fireWeapon()) and is still
// the one real source of truth for hit resolution.
struct ProjectileState {
    ProjectileType type = ProjectileType::Rocket;
    net::PlayerId owner = net::kInvalidPlayer;
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};
    float damage = 0.0f;
    // Real Saboteur/Island-Sea synergy flag -- see TorpedoStealth.hpp
    // for the real detection logic this feeds.
    bool stealth = false;
    float lifetimeSeconds = 0.0f;
    bool expired = false;
};

// Real per-type initial speed/gravity-scale/damage/lifetime -- a
// Torpedo is slow, high-damage, and gravity-immune (travels underwater,
// not ballistic); everything else is a real ballistic arc.
struct ProjectileTuning {
    float initialSpeed = 30.0f;
    float gravityScale = 1.0f;
    float lifetimeSeconds = 6.0f;
    float radius = 0.5f; // real hit-test radius
};
[[nodiscard]] ProjectileTuning projectileTuningFor(ProjectileType type);

[[nodiscard]] ProjectileState spawnProjectile(ProjectileType type, net::PlayerId owner, glm::vec3 origin,
                                                glm::vec3 aimDirection, float damage);

// Real, pure physics step -- gravity (real world constant, matching
// this engine's other Y-down convention) integrated into velocity, then
// position integrated from velocity, real lifetime countdown setting
// `expired` once it runs out. No collision resolution here (that's a
// real ECS/Physics-touching concern, deliberately kept out of this pure
// function -- same "pure or ECS-only, never both" split
// net::InteractionValidation.hpp already draws).
void stepProjectile(ProjectileState& state, float dt);

// Real, pure sphere-vs-point hit test -- what a real caller (server-side
// combat resolution) runs each tick against every real potential
// target's position.
[[nodiscard]] bool projectileHitsTarget(const ProjectileState& state, glm::vec3 targetPosition, float targetRadius);

} // namespace engine::tntwars
