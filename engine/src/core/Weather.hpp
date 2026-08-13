#pragma once

#include "core/SceneTypes.hpp"

namespace engine::core {

// Kronos ("Rendering Fidelity Foundation" Phase 1.1): a real, pure,
// fully unit-testable dynamic weather system -- zero GPU/window
// dependency, same discipline core::TimeOfDay already established for
// day/night. Composes *on top of* whatever SceneLighting the caller
// already produced (TimeOfDay's own cycle, a trailer beat's custom zone
// lighting, a flat Studio default) rather than replacing it outright --
// see applyWeather()'s own comment for why Clear weather is a real,
// exact no-op instead of silently overwriting the base scene's look.
enum class WeatherKind : uint8_t { Clear, Rain, Snow, Storm };

// The real, tunable "look" of one weather kind. `overrideStrength` is
// the load-bearing field: 0 means this profile has zero visual effect
// (Clear's own value) and every other field below is simply unused at
// that point; anything above 0 is how strongly this profile's own
// fog/sky/desaturation values blend over the base scene's own, see
// applyWeather().
struct WeatherProfile {
    float overrideStrength = 0.0f; // 0..1
    glm::vec3 fogColor{0.6f, 0.65f, 0.75f};
    float fogDensity = 0.0f;
    glm::vec3 skyZenithColor{0.25f, 0.45f, 0.85f};
    glm::vec3 skyHorizonColor{0.75f, 0.80f, 0.85f};
    float ambientDesaturation = 0.0f; // 0..1, blends ambient/ambientGround toward their own real luminance-gray
    float precipitationRate = 0.0f;   // particles/sec -- a real hint for whoever owns the live ParticleSystem (Renderer never spawns particles itself, see Renderer::currentWeatherProfile()'s own comment)
    float wetness = 0.0f;             // 0..1 -- ground-facing roughness reduction, see SceneUBO::renderFlags.z's own comment
};

// Real, named presets -- one per WeatherKind. Clear is deliberately
// `overrideStrength = 0`, not "sunny-day values that happen to look
// clear," so it composes as a true identity in applyWeather() regardless
// of what the base scene's own fog/sky already were.
[[nodiscard]] WeatherProfile weatherProfileFor(WeatherKind kind);

// A real, smoothly-blended transition between two profiles.
// `fromProfile` is a real snapshot (not just a WeatherKind) of whatever
// was actually on screen the moment the current transition started --
// see setWeatherTarget()'s own comment for why this matters for
// mid-transition retargeting.
struct WeatherState {
    WeatherProfile fromProfile = weatherProfileFor(WeatherKind::Clear);
    WeatherKind toKind = WeatherKind::Clear;
    float transitionElapsed = 0.0f;
    float transitionDuration = 0.0f;
};

// Starts a real transition toward `target` over `transitionSeconds`. A
// real, honest no-op if `target` already equals the current fully-
// settled target. Retargeting mid-transition (e.g. Rain -> Storm called
// while still blending Clear -> Rain) snapshots the *current blended
// profile* as the new starting point (via currentBlendedProfile()) --
// this guarantees a real, smooth continuation with no visual jump,
// rather than restarting from either endpoint of the interrupted
// transition.
void setWeatherTarget(WeatherState& state, WeatherKind target, float transitionSeconds);

// Advances `state.transitionElapsed` by `dt` real seconds, clamped to
// `state.transitionDuration`. A `transitionDuration <= 0` is a real,
// honest instant snap (weatherTransitionT() below returns 1.0
// unconditionally in that case), not a divide-by-zero.
void tickWeather(WeatherState& state, float dt);

// Pure -- 0..1 progress through the current transition. Always 1.0 if
// `transitionDuration <= 0`.
[[nodiscard]] float weatherTransitionT(const WeatherState& state);

// Pure -- the real, continuously-blended profile "in flight" right now:
// lerp(state.fromProfile, weatherProfileFor(state.toKind), weatherTransitionT(state)).
[[nodiscard]] WeatherProfile currentBlendedProfile(const WeatherState& state);

// Pure -- composes `weather` on top of `base`. A real, exact identity
// (`return base;`, untouched) whenever `weather.overrideStrength <= 0`
// (Clear, or a transition that has fully settled back to Clear) -- this
// is why weatherProfileFor(Clear)'s other fields are irrelevant: they're
// never read. Otherwise blends fog/sky by `overrideStrength` and
// desaturates ambient/ambientGround toward their own real per-channel
// average (luminance-preserving gray, not a fixed color) scaled by
// `ambientDesaturation * overrideStrength`.
[[nodiscard]] SceneLighting applyWeather(const SceneLighting& base, const WeatherProfile& weather);

} // namespace engine::core
