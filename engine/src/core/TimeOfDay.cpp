#include "core/TimeOfDay.hpp"

#include <algorithm>
#include <cmath>

namespace engine::core {

namespace {
constexpr float kPi = 3.14159265359f;
constexpr float kSunriseHour = 6.0f;
constexpr float kMaxSunElevationDegrees = 75.0f;
} // namespace

void tickTimeOfDay(TimeOfDayState& state, float dt, float dayLengthSeconds) {
    if (dayLengthSeconds <= 0.0f) return;
    float hoursPerSecond = 24.0f / dayLengthSeconds;
    state.hours = std::fmod(state.hours + dt * hoursPerSecond, 24.0f);
    if (state.hours < 0.0f) state.hours += 24.0f;
}

SceneLighting computeLightingForTimeOfDay(float hours) {
    SceneLighting lighting;

    // Real elevation/azimuth sun model -- a single continuous formula
    // covering the full 24h cycle (elevation peaks at solar noon,
    // crosses zero at kSunriseHour/its 12-hours-later sunset, and goes
    // negative -- sun below the horizon -- through the night), not a
    // piecewise "day" vs "night" branch that could show a seam at the
    // boundary.
    float elevationRad =
        glm::radians(kMaxSunElevationDegrees) * std::sin(2.0f * kPi * (hours - kSunriseHour) / 24.0f);
    float azimuthRad = 2.0f * kPi * (hours / 24.0f);

    glm::vec3 sunDir = glm::normalize(
        glm::vec3(std::cos(elevationRad) * std::sin(azimuthRad), std::sin(elevationRad), std::cos(elevationRad) * std::cos(azimuthRad)));
    lighting.directionWS = -sunDir; // SceneLighting::directionWS is the direction the light *travels*, sun-to-ground

    // dayFactor: 0 well below the horizon, ramps to 1 within the first
    // ~30 degrees of real sunrise -- a fast, real dawn/dusk transition
    // with long, stable daytime/nighttime either side of it, not a slow
    // fade across the whole cycle.
    float dayFactor = std::clamp(std::sin(elevationRad) * 2.0f, 0.0f, 1.0f);
    // noonBlend: 0 near the horizon (dawn/dusk, warm colors), 1 once the
    // sun is well up (cooler, brighter midday colors) -- independent of
    // dayFactor so dawn/dusk's real warm-color character shows up during
    // the transition, not just a dimmer version of noon.
    float noonBlend = std::clamp(std::sin(elevationRad) * 1.4f, 0.0f, 1.0f);

    glm::vec3 nightColor(0.15f, 0.20f, 0.35f);
    glm::vec3 dawnDuskColor(1.0f, 0.55f, 0.30f);
    glm::vec3 noonColor(1.0f, 0.97f, 0.92f);
    glm::vec3 dayColor = glm::mix(dawnDuskColor, noonColor, noonBlend);
    lighting.color = glm::mix(nightColor, dayColor, dayFactor);

    constexpr float kMinIntensity = 0.15f; // real, dim moonlight -- never literally zero
    constexpr float kMaxIntensity = 4.0f;
    lighting.intensity = glm::mix(kMinIntensity, kMaxIntensity, dayFactor);

    lighting.ambient = glm::mix(glm::vec3(0.03f, 0.04f, 0.08f), glm::vec3(0.12f, 0.15f, 0.22f), dayFactor);
    lighting.ambientGround = glm::mix(glm::vec3(0.02f, 0.02f, 0.03f), glm::vec3(0.09f, 0.08f, 0.07f), dayFactor);

    // Real, if modest, fog -- thicker and cooler at night (real
    // atmospheric convention), thinner during the day; see
    // shaders/scene.frag's applyFog() for the exact exponential-squared
    // formula these density values feed.
    lighting.fogColor = glm::mix(glm::vec3(0.05f, 0.06f, 0.10f), glm::vec3(0.65f, 0.70f, 0.78f), dayFactor);
    lighting.fogDensity = glm::mix(0.012f, 0.004f, dayFactor);

    lighting.skyZenithColor = glm::mix(glm::vec3(0.02f, 0.03f, 0.08f), glm::vec3(0.25f, 0.45f, 0.85f), dayFactor);
    glm::vec3 dawnDuskHorizon(0.95f, 0.55f, 0.35f);
    glm::vec3 dayHorizon(0.75f, 0.80f, 0.85f);
    glm::vec3 horizonDayColor = glm::mix(dawnDuskHorizon, dayHorizon, noonBlend);
    lighting.skyHorizonColor = glm::mix(glm::vec3(0.05f, 0.06f, 0.12f), horizonDayColor, dayFactor);

    return lighting;
}

} // namespace engine::core
