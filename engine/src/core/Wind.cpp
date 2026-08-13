#include "core/Wind.hpp"

#include <cmath>

#include "core/Components.hpp"

namespace engine::core {

glm::vec3 currentWindVector(const WindState& wind, float totalTimeSeconds) {
    glm::vec3 flatDir = glm::vec3(wind.direction.x, 0.0f, wind.direction.z);
    float len = glm::length(flatDir);
    glm::vec3 dir = len > 1e-5f ? flatDir / len : glm::vec3(1.0f, 0.0f, 0.0f);
    // Real, slow gust modulation -- 0.7..1.0, never fully calm (wind never
    // truly stops) and never a flat constant either.
    float gust = 0.85f + 0.15f * std::sin(totalTimeSeconds * 0.35f);
    return dir * wind.strength * gust;
}

void tickWindSway(ECS& ecs, const WindState& wind, float totalTimeSeconds) {
    glm::vec3 windVec = currentWindVector(wind, totalTimeSeconds);
    float windStrength = glm::length(windVec);
    glm::vec3 windDir = windStrength > 1e-4f ? windVec / windStrength : glm::vec3(1.0f, 0.0f, 0.0f);
    // Real horizontal axis perpendicular to the wind's own direction --
    // leaning around this axis tilts a blade *along* the wind, matching
    // how real grass/flora actually bends.
    glm::vec3 swayAxis = glm::normalize(glm::cross(windDir, glm::vec3(0.0f, 1.0f, 0.0f)));

    auto view = ecs.view<WindSway, Transform>();
    for (auto entity : view) {
        auto& sway = view.get<WindSway>(entity);
        auto& transform = view.get<Transform>(entity);
        float leanRadians =
            std::sin(totalTimeSeconds * sway.speed + sway.phase) * glm::radians(sway.amplitudeDegrees) * windStrength;
        transform.rotation = sway.baseRotation * glm::angleAxis(leanRadians, swayAxis);
    }
}

void tickAtmosphericDustWind(ECS& ecs, const WindState& wind, float totalTimeSeconds) {
    glm::vec3 windVec = currentWindVector(wind, totalTimeSeconds);
    auto view = ecs.view<AtmosphericDustEmitter, ParticleEmitter>();
    for (auto entity : view) {
        auto& dust = view.get<AtmosphericDustEmitter>(entity);
        auto& emitter = view.get<ParticleEmitter>(entity);
        emitter.settings.velocityMin = dust.baseVelocityMin + windVec;
        emitter.settings.velocityMax = dust.baseVelocityMax + windVec;
    }
}

} // namespace engine::core
