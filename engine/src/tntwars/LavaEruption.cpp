#include "tntwars/LavaEruption.hpp"

#include <cmath>

namespace engine::tntwars {

void tickLavaEruption(LavaEruptionState& state, float dt) {
    if (dt <= 0.0f) return;
    state.cycleTimer += dt;
    if (state.cycleTimer >= LavaEruptionState::kEruptionIntervalSeconds) {
        // Real wraparound, not a hard reset to 0 -- preserves real
        // overshoot (a large dt on a slow tick shouldn't lose time),
        // matching core::TimeOfDay's own real wraparound convention.
        state.cycleTimer = std::fmod(state.cycleTimer, LavaEruptionState::kEruptionIntervalSeconds);
    }
}

bool isWithinEruptionDamageRadius(glm::vec3 playerPosition, glm::vec3 eruptionCenter, float radius) {
    glm::vec3 delta = playerPosition - eruptionCenter;
    float distanceSq = delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;
    return distanceSq <= radius * radius;
}

} // namespace engine::tntwars
