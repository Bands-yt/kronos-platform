#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "core/ECS.hpp"
#include "core/ParticleSystem.hpp"

namespace engine::core {

// Kronos ("Environmental Detail" world-building): a real, pure, fully
// unit-testable ambient wind system -- same "zero GPU/window dependency"
// discipline core::Weather/core::TimeOfDay already established. Drives
// two real, independent consumers: vegetation sway (tickWindSway() below,
// this file) and atmospheric-particle drift bias (Application's own
// pretick hook, which reads currentWindVector() directly rather than
// duplicating the gust math).
struct WindState {
    glm::vec3 direction{1.0f, 0.0f, 0.0f}; // real, horizontal (Y ignored/renormalized), the wind's own real base heading
    float strength = 0.4f;                 // real, 0..1-ish base gust intensity
};

// Pure -- the real, gust-modulated wind vector at `totalTimeSeconds`:
// direction (renormalized, Y forced to 0 -- wind is horizontal) scaled by
// `strength` and a real, slow sinusoidal gust factor (0.7..1.0, never
// fully calm, never a flat constant either) so both vegetation sway and
// particle drift breathe over time instead of holding one fixed value
// forever.
[[nodiscard]] glm::vec3 currentWindVector(const WindState& wind, float totalTimeSeconds);

// Kronos ("Environmental Detail" world-building): real per-entity sway
// state -- attach to any entity with a Transform to make it lean in the
// wind (grass tufts, crystal flora, anything thin/rooted). `baseRotation`
// is the real, authored resting orientation tickWindSway() sways *around*
// -- captured once at spawn time, never itself mutated, so an entity's
// own real scatter-placement rotation isn't erased by the sway.
// `phase` is a real per-instance offset (typically hashed from world
// position) so a whole cluster doesn't sway in obviously-synchronized
// unison.
struct WindSway {
    glm::quat baseRotation{1.0f, 0.0f, 0.0f, 0.0f};
    float amplitudeDegrees = 5.0f;
    float speed = 1.4f;
    float phase = 0.0f;
};

// Real per-tick sway application -- every real entity with both WindSway
// and Transform gets `transform.rotation = sway.baseRotation *
// angleAxis(leanAngle, swayAxis)`, where `swayAxis` is the real horizontal
// axis perpendicular to the current wind direction (so a swaying blade
// leans *along* the wind's own real direction of travel, not sideways to
// it) and `leanAngle` oscillates via a real sine wave scaled by both
// `sway.amplitudeDegrees` and the wind's own real current strength (calm
// wind -> barely any sway, a real gust -> a real, visible lean). A real,
// honest no-op (zero cost beyond the empty view iteration) when no entity
// has a WindSway component at all -- safe to call unconditionally every
// frame regardless of which scene/map is active.
void tickWindSway(ECS& ecs, const WindState& wind, float totalTimeSeconds);

// Kronos ("Environmental Detail" world-building): real, empty tag --
// marks a ParticleEmitter entity as real, wind-biased atmospheric dust
// (see tickAtmosphericDustWind() below), distinct from other real
// emitters (e.g. Application's own weather rain/snow emitter) that
// already manage their own velocity fully independently each tick and
// must never have a second, competing wind bias layered on top.
// `baseVelocityMin/Max` is the real, intrinsic drift (independent of
// wind -- what the dust would do in dead calm); tickAtmosphericDustWind()
// adds the real current wind vector on top each tick, it never replaces
// this base.
struct AtmosphericDustEmitter {
    glm::vec3 baseVelocityMin{-0.15f, 0.05f, -0.15f};
    glm::vec3 baseVelocityMax{0.15f, 0.25f, 0.15f};
};

// Real per-tick wind bias -- every entity with both AtmosphericDustEmitter
// and ParticleEmitter gets its own real emitter.settings.velocityMin/Max
// set to (base + currentWindVector()), so dust drifts with the map's own
// real ambient wind instead of a fixed, static direction. A real, honest
// no-op when no such entity exists -- safe to call unconditionally every
// frame regardless of which scene/map is active, same convention
// tickWindSway() above already establishes.
void tickAtmosphericDustWind(ECS& ecs, const WindState& wind, float totalTimeSeconds);

} // namespace engine::core
