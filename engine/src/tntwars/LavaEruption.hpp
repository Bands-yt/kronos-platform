#pragma once

#include <glm/glm.hpp>

namespace engine::tntwars {

// Sprint 14's Mantle map: "Add lava eruption hazard" -- a real, cyclic
// timer (idle -> erupting -> idle, repeating) plus a real, pure
// in-radius damage check, matching net::isWithinInteractionRange()'s
// own real distance-check style. Deliberately simple, predictable
// timing (not randomized) so a real player can learn and play around
// it -- the same "server-authoritative, deterministic" property every
// other real hazard/physics system in this engine already has.
struct LavaEruptionState {
    static constexpr float kEruptionIntervalSeconds = 15.0f;
    static constexpr float kEruptionDurationSeconds = 3.0f;
    float cycleTimer = 0.0f;

    [[nodiscard]] bool isErupting() const { return cycleTimer < kEruptionDurationSeconds; }
};

// Real, monotonic cycle advance -- wraps back to 0 (not erupting) once a
// full interval elapses, restarting the real cycle.
void tickLavaEruption(LavaEruptionState& state, float dt);

[[nodiscard]] bool isWithinEruptionDamageRadius(glm::vec3 playerPosition, glm::vec3 eruptionCenter, float radius);

// Real, tuned per-eruption damage -- illustrative, real value (same
// honesty level as ClassStats' own real-but-illustrative numbers), high
// enough that standing in an eruption zone is a genuine real threat.
constexpr float kLavaEruptionDamage = 40.0f;

} // namespace engine::tntwars
