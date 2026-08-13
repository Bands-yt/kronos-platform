#include "tntwars/SpaceTraversal.hpp"

namespace engine::tntwars {

void tickBoosterPad(BoosterPadState& pad, float dt) {
    if (dt <= 0.0f || pad.cooldownSecondsRemaining <= 0.0f) return;
    pad.cooldownSecondsRemaining -= dt;
    if (pad.cooldownSecondsRemaining < 0.0f) pad.cooldownSecondsRemaining = 0.0f;
}

std::optional<glm::vec3> triggerBoosterPad(BoosterPadState& pad, glm::vec3 playerPosition) {
    if (pad.cooldownSecondsRemaining > 0.0f) return std::nullopt;
    if (glm::length(playerPosition - pad.position) > pad.triggerRadius) return std::nullopt;

    pad.cooldownSecondsRemaining = kBoosterPadCooldownSeconds;
    return pad.direction * pad.launchStrength;
}

bool isInsideZeroGravityZone(const ZeroGravityZone& zone, glm::vec3 position) {
    return glm::length(position - zone.position) <= zone.radius;
}

glm::vec3 zeroGravityCompensation(const ZeroGravityZone& zone, glm::vec3 position, glm::vec3 standardGravity,
                                   float dt) {
    if (dt <= 0.0f || !isInsideZeroGravityZone(zone, position)) return glm::vec3(0.0f);
    return -standardGravity * zone.gravityCancelFraction * dt;
}

glm::vec3 gravityWellPull(const GravityWellState& well, glm::vec3 position, float dt) {
    if (dt <= 0.0f) return glm::vec3(0.0f);
    glm::vec3 toCenter = well.position - position;
    float distance = glm::length(toCenter);
    if (distance <= 0.0001f || distance > well.radius) return glm::vec3(0.0f);

    float falloff = 1.0f - (distance / well.radius);
    glm::vec3 direction = toCenter / distance;
    return direction * (well.maxAccel * falloff * dt);
}

} // namespace engine::tntwars
