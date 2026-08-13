#include "core/Weather.hpp"

#include <algorithm>

namespace engine::core {

WeatherProfile weatherProfileFor(WeatherKind kind) {
    switch (kind) {
        case WeatherKind::Clear:
            // Real, exact match to SceneLighting{}'s own defaults --
            // never actually read (overrideStrength == 0 short-circuits
            // applyWeather() before any other field matters), kept
            // sensible purely so a debug/inspection tool printing this
            // profile directly sees a real, honest "clear sky" rather
            // than zeroed-out garbage.
            return WeatherProfile{
                /*overrideStrength=*/0.0f, /*fogColor=*/glm::vec3(0.6f, 0.65f, 0.75f),
                /*fogDensity=*/0.0f,       /*skyZenithColor=*/glm::vec3(0.25f, 0.45f, 0.85f),
                /*skyHorizonColor=*/glm::vec3(0.75f, 0.80f, 0.85f), /*ambientDesaturation=*/0.0f,
                /*precipitationRate=*/0.0f, /*wetness=*/0.0f};
        case WeatherKind::Rain:
            return WeatherProfile{
                /*overrideStrength=*/0.65f, /*fogColor=*/glm::vec3(0.45f, 0.48f, 0.53f),
                /*fogDensity=*/0.012f,      /*skyZenithColor=*/glm::vec3(0.28f, 0.32f, 0.40f),
                /*skyHorizonColor=*/glm::vec3(0.50f, 0.53f, 0.58f), /*ambientDesaturation=*/0.35f,
                /*precipitationRate=*/220.0f, /*wetness=*/0.70f};
        case WeatherKind::Snow:
            return WeatherProfile{
                /*overrideStrength=*/0.55f, /*fogColor=*/glm::vec3(0.75f, 0.78f, 0.85f),
                /*fogDensity=*/0.016f,      /*skyZenithColor=*/glm::vec3(0.55f, 0.58f, 0.66f),
                /*skyHorizonColor=*/glm::vec3(0.82f, 0.84f, 0.90f), /*ambientDesaturation=*/0.45f,
                /*precipitationRate=*/90.0f, /*wetness=*/0.15f};
        case WeatherKind::Storm:
            return WeatherProfile{
                /*overrideStrength=*/0.90f, /*fogColor=*/glm::vec3(0.22f, 0.24f, 0.28f),
                /*fogDensity=*/0.026f,      /*skyZenithColor=*/glm::vec3(0.10f, 0.11f, 0.14f),
                /*skyHorizonColor=*/glm::vec3(0.28f, 0.30f, 0.34f), /*ambientDesaturation=*/0.60f,
                /*precipitationRate=*/380.0f, /*wetness=*/0.85f};
    }
    return WeatherProfile{};
}

void setWeatherTarget(WeatherState& state, WeatherKind target, float transitionSeconds) {
    if (target == state.toKind && weatherTransitionT(state) >= 1.0f) return; // real, honest no-op -- already there
    state.fromProfile = currentBlendedProfile(state); // real snapshot of wherever we actually are right now
    state.toKind = target;
    state.transitionElapsed = 0.0f;
    state.transitionDuration = transitionSeconds;
}

void tickWeather(WeatherState& state, float dt) {
    float clampMax = std::max(state.transitionDuration, 0.0f);
    state.transitionElapsed = std::clamp(state.transitionElapsed + dt, 0.0f, clampMax);
}

float weatherTransitionT(const WeatherState& state) {
    if (state.transitionDuration <= 0.0f) return 1.0f;
    return std::clamp(state.transitionElapsed / state.transitionDuration, 0.0f, 1.0f);
}

WeatherProfile currentBlendedProfile(const WeatherState& state) {
    WeatherProfile to = weatherProfileFor(state.toKind);
    float t = weatherTransitionT(state);
    const WeatherProfile& from = state.fromProfile;
    WeatherProfile out;
    out.overrideStrength = glm::mix(from.overrideStrength, to.overrideStrength, t);
    out.fogColor = glm::mix(from.fogColor, to.fogColor, t);
    out.fogDensity = glm::mix(from.fogDensity, to.fogDensity, t);
    out.skyZenithColor = glm::mix(from.skyZenithColor, to.skyZenithColor, t);
    out.skyHorizonColor = glm::mix(from.skyHorizonColor, to.skyHorizonColor, t);
    out.ambientDesaturation = glm::mix(from.ambientDesaturation, to.ambientDesaturation, t);
    out.precipitationRate = glm::mix(from.precipitationRate, to.precipitationRate, t);
    out.wetness = glm::mix(from.wetness, to.wetness, t);
    return out;
}

SceneLighting applyWeather(const SceneLighting& base, const WeatherProfile& weather) {
    if (weather.overrideStrength <= 0.0f) return base; // real, exact identity -- see this function's own header comment
    SceneLighting out = base;
    float s = std::clamp(weather.overrideStrength, 0.0f, 1.0f);
    out.fogColor = glm::mix(base.fogColor, weather.fogColor, s);
    out.fogDensity = glm::mix(base.fogDensity, weather.fogDensity, s);
    out.skyZenithColor = glm::mix(base.skyZenithColor, weather.skyZenithColor, s);
    out.skyHorizonColor = glm::mix(base.skyHorizonColor, weather.skyHorizonColor, s);

    float desatAmount = std::clamp(weather.ambientDesaturation * s, 0.0f, 1.0f);
    float ambientGray = (base.ambient.r + base.ambient.g + base.ambient.b) / 3.0f;
    out.ambient = glm::mix(base.ambient, glm::vec3(ambientGray), desatAmount);
    float ambientGroundGray = (base.ambientGround.r + base.ambientGround.g + base.ambientGround.b) / 3.0f;
    out.ambientGround = glm::mix(base.ambientGround, glm::vec3(ambientGroundGray), desatAmount);

    return out;
}

} // namespace engine::core
