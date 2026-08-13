#pragma once

#include <glm/glm.hpp>

#include "core/ECS.hpp"

namespace engine::tntwars {

// Kronos ("Lighting Polish" world-building, "station flicker panels"):
// real, per-entity animated emissive state -- the live half of the real
// gap SpaceMapTerrain.cpp's own header comment on DerelictStation light
// panels explicitly flagged as "not built here." Attach to any entity
// with a real core::Renderable to make its own real emissiveIntensity
// flicker over time (a station light panel, a damaged console, any
// "electrical, imperfect" light source) -- same real "small tag
// component + a pure tick function" shape core::WindSway already
// establishes.
struct FlickerLight {
    glm::vec3 baseColor{0.65f, 0.85f, 1.0f};
    float baseIntensity = 0.6f;
    // Real, per-instance time offset (typically hashed from world
    // position/index) so a whole bank of panels doesn't flicker in
    // obviously-synchronized unison -- same real purpose as
    // core::WindSway::phase.
    float phase = 0.0f;
};

// Pure -- a real, multiplicative 0..1 factor combining a gentle, always-
// present sinusoidal flicker (never fully steady, reads as "powered but
// imperfect") with an occasional, real, hashed dropout window (roughly a
// 6% chance per ~0.5-second window to dip to near-dark, reading as a
// real failing connection) -- deterministic given (phase,
// totalTimeSeconds), so this is fully unit-testable without any live
// clock.
[[nodiscard]] float flickerIntensityFactor(float phase, float totalTimeSeconds);

// Real per-tick sync -- every entity with both FlickerLight and
// core::Renderable gets `renderable.emissiveColor = light.baseColor` and
// `renderable.emissiveIntensity = light.baseIntensity *
// flickerIntensityFactor(...)`. A real, honest no-op (zero cost beyond an
// empty ECS view iteration) when no entity has a FlickerLight component
// at all -- safe to call unconditionally every frame, same convention
// core::tickWindSway() already establishes.
void tickFlickerLights(core::ECS& ecs, float totalTimeSeconds);

} // namespace engine::tntwars
