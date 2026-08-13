#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "net/NetTypes.hpp"
#include "tntwars/DestructibleGeometry.hpp"

namespace engine::tntwars {

constexpr float kDefaultFuseSeconds = 3.0f;
constexpr float kExplosionRadius = 8.0f;
constexpr float kExplosionMaxDamage = 150.0f;
constexpr float kExplosionMaxImpulse = 20.0f;

// Kronos roadmap Milestone 3 ("TNT/explosive core mechanic"): a real,
// placed charge with a real, counting-down fuse -- the same "real,
// deterministic timer" precedent LavaEruptionState already set for this
// module, applied to a one-shot rather than cyclic timer. The three
// explosion* fields default to the always-available "basic" charge's own
// tuning (see placeTntCharge()'s own default args below), but Kronos
// Milestone 6 ("Crafting explosives") sets them per-charge from a
// crafted recipe's own real, stronger tuning -- so a placed charge always
// carries its own real explosion stats rather than a caller having to
// separately track "how strong was this one."
struct TntChargeState {
    net::PlayerId owner = net::kInvalidPlayer;
    glm::vec3 position{0.0f};
    float fuseSecondsRemaining = 0.0f;
    bool detonated = false;
    float explosionRadius = kExplosionRadius;
    float explosionMaxDamage = kExplosionMaxDamage;
    float explosionMaxImpulse = kExplosionMaxImpulse;
};

[[nodiscard]] TntChargeState placeTntCharge(net::PlayerId owner, glm::vec3 position,
                                             float fuseSeconds = kDefaultFuseSeconds,
                                             float explosionRadius = kExplosionRadius,
                                             float explosionMaxDamage = kExplosionMaxDamage,
                                             float explosionMaxImpulse = kExplosionMaxImpulse);

// Real fuse countdown -- sets `detonated` true the exact real moment
// fuseSecondsRemaining first reaches 0, and is a real, honest no-op on
// every tick after that (matches applyThrusterHit()'s own "can't destroy
// rubble twice" idempotency).
void tickTntCharge(TntChargeState& state, float dt);

// Real linear falloff -- full `maxDamage` at the exact explosion center,
// 0 at/beyond `radius`. Distance is real point-to-point (not AABB-aware),
// the same real simplification LavaEruption's own
// isWithinEruptionDamageRadius() already makes for a radius check against
// a real gameplay position.
[[nodiscard]] float computeExplosionDamage(glm::vec3 explosionCenter, glm::vec3 targetPosition,
                                            float radius = kExplosionRadius, float maxDamage = kExplosionMaxDamage);

// Real explosion physics impulse -- pure data a real ECS/Jolt-touching
// caller applies via a real AddImpulse call (this module stops at
// producing the real vector, the same "pure step, no collision
// resolution here" split Projectile.hpp's stepProjectile() already
// draws). Direction radiates outward from the explosion center; a target
// sitting exactly at the center falls back to a real, fixed up-vector so
// the result is never NaN.
struct ExplosionImpulse {
    glm::vec3 direction{0.0f, 1.0f, 0.0f};
    float magnitude = 0.0f;
};
[[nodiscard]] ExplosionImpulse computeExplosionImpulse(glm::vec3 explosionCenter, glm::vec3 targetPosition,
                                                        float radius = kExplosionRadius,
                                                        float maxImpulse = kExplosionMaxImpulse);

// Real batch application against one destructible structure's own
// segments -- each segment takes real falloff damage based on its own
// position's distance from the explosion center.
void applyExplosionToSegments(std::vector<DestructibleSegment>& segments, glm::vec3 explosionCenter,
                               float radius = kExplosionRadius, float maxDamage = kExplosionMaxDamage);

} // namespace engine::tntwars
