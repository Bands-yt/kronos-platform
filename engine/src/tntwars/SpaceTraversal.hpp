#pragma once

#include <optional>

#include <glm/glm.hpp>

namespace engine::tntwars {

// Kronos ("Space Map Bible" v1.0, Section III "Traversal Systems"): real,
// pure, fully unit-testable traversal mechanics specific to Space Map's
// own real "defined by height and void, not width" identity. Orbital
// Rails deliberately reuse the *existing* ZipLineState/advanceZipLineRider
// arc-length-parameterized curve-riding system (see SpaceMapTerrain.hpp's
// own spawnSpaceMapOrbitalRails() -- the Space Map Bible's own spec
// literally calls out "same fix as Sky Map" for this exact mechanic), so
// this file covers the three mechanics that don't already have a real,
// reusable engine system: Booster Pads (directional propulsion), Zero-G
// Zones (local gravity cancellation), and Gravity Wells (local
// attraction). core::Physics only exposes one *global* gravity vector
// (no per-body Jolt GravityFactor override exists in this engine) --
// both Zero-G Zones and Gravity Wells are deliberately implemented as
// real, local per-tick velocity impulses applied by a live caller
// (Application's own tick), not a physics-engine-level gravity change,
// so neither one ever affects any other entity/scene.

// Real, directional launch pad -- same real "trigger radius + cooldown,
// fires once per cooldown while a player is in range" shape
// JumpPadState (Movement.hpp) already establishes, but launches along an
// arbitrary real world direction instead of always straight up: open
// void has no privileged "up," so Space Map's own real booster identity
// needs a real direction, not just a height.
struct BoosterPadState {
    glm::vec3 position{0.0f};
    glm::vec3 direction{0.0f, 0.0f, 1.0f}; // real, expected pre-normalized
    float triggerRadius = 3.0f;
    float launchStrength = 24.0f;
    float cooldownSecondsRemaining = 0.0f; // real-0 while ready to fire again
};
constexpr float kBoosterPadCooldownSeconds = 1.2f;

void tickBoosterPad(BoosterPadState& pad, float dt);
// Real trigger -- returns the real launch velocity vector
// (direction * launchStrength) if `playerPosition` is within
// `triggerRadius` and the pad is off cooldown (real-starts the cooldown
// on success); nullopt (a real, honest no-op) otherwise.
[[nodiscard]] std::optional<glm::vec3> triggerBoosterPad(BoosterPadState& pad, glm::vec3 playerPosition);

// Real, local gravity-cancelling volume.
struct ZeroGravityZone {
    glm::vec3 position{0.0f};
    float radius = 15.0f;
    float gravityCancelFraction = 0.85f; // 0 = no real effect, 1 = fully cancels gravity's own per-tick pull
};

[[nodiscard]] bool isInsideZeroGravityZone(const ZeroGravityZone& zone, glm::vec3 position);
// Pure -- the real per-tick compensating impulse (opposite of
// `standardGravity`, scaled by the zone's own real cancelFraction and
// `dt`) a live caller should apply via Physics::applyImpulse() to
// counteract that same tick's own standard gravity pull while inside the
// zone. A real, honest zero vector when `position` is outside the zone.
[[nodiscard]] glm::vec3 zeroGravityCompensation(const ZeroGravityZone& zone, glm::vec3 position,
                                                  glm::vec3 standardGravity, float dt);

// Real, local attraction volume -- opposite sign from a Zero-G Zone.
// Falls off *linearly* with distance (real `maxAccel` at the exact
// center, 0 at `radius`) -- a real, deliberately simple/stable falloff,
// not real inverse-square (which spikes toward infinity at the exact
// center, an honest simplification for gameplay-feel purposes over
// physical accuracy).
struct GravityWellState {
    glm::vec3 position{0.0f};
    float radius = 20.0f;
    float maxAccel = 8.0f;
};

// Pure -- the real per-tick pull *impulse* (already scaled by `dt`) a
// live caller should apply via Physics::applyImpulse(); a real, honest
// zero vector outside `radius` or at zero distance (no real direction to
// pull in in that degenerate case).
[[nodiscard]] glm::vec3 gravityWellPull(const GravityWellState& well, glm::vec3 position, float dt);

} // namespace engine::tntwars
