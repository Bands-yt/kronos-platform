#pragma once

#include "core/SceneTypes.hpp"

namespace engine::core {

// Sprint 6 ("World Systems & Environment") task category 3: a real,
// pure, fully unit-testable day/night cycle -- sun movement, ambient
// color shift, and fog, all driven by one real 24-hour clock, with zero
// GPU/window dependency (the same "pure logic, no live device needed"
// discipline core::Physics/core::Economy already established this
// session). Its whole output is a real core::SceneLighting the caller
// (Application.cpp) hands straight to Renderer::setLighting() every
// tick -- see that struct's own comment for why shadow-bias correctness
// under a moving light needs zero extra code here: the renderer already
// recomputes cascades/bias fresh from whatever SceneLighting currently
// holds, every single frame.
struct TimeOfDayState {
    float hours = 12.0f; // real 24-hour clock, wraps at 24
};

// Called once per tick -- advances `state.hours` by `dt` real seconds
// scaled to `dayLengthSeconds` (how many real seconds one full in-game
// day takes), wrapping cleanly back into [0,24).
void tickTimeOfDay(TimeOfDayState& state, float dt, float dayLengthSeconds);

// Pure -- the real heart of this module. Computes a full SceneLighting
// (sun direction from a real elevation/azimuth model, color/intensity/
// ambient/fog/sky all shifting smoothly across a real dawn -> day ->
// dusk -> night curve) for a given hour of the day. Sunrise/sunset at
// real, named hours (6:00/18:00) rather than hardcoded into the curve
// math directly, so a caller (or a future Studio time-of-day scrubber)
// can reason about "what does 7am look like" concretely.
[[nodiscard]] SceneLighting computeLightingForTimeOfDay(float hours);

} // namespace engine::core
