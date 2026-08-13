#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "tntwars/SpaceMapTerrain.hpp"

namespace engine::tntwars {

// Kronos ("Lighting Polish" world-building, "give the world atmospheric
// depth"): real, pure, fully unit-testable per-position atmosphere
// lookup -- same "zero renderer dependency" discipline core::Weather/
// core::Wind already established. A zone is a real position+radius
// override on top of a map's own real baseline (`defaultSample`,
// supplied by the caller -- see Application's own live tick for exactly
// what that baseline is per map); it never accumulates with other
// overlapping zones (a real, deliberate simplification -- the single
// strongest-weighted zone wins outright and blends toward the baseline
// by its own real soft-edged weight, avoiding a muddy multi-zone
// average).
struct AtmosphereZone {
    glm::vec3 position{0.0f};
    float radius = 20.0f;
    glm::vec3 fogColor{0.6f, 0.65f, 0.75f};
    float fogDensity = 0.0f;
    float exposure = 1.0f;
};

struct AtmosphereSample {
    glm::vec3 fogColor{0.6f, 0.65f, 0.75f};
    float fogDensity = 0.0f;
    float exposure = 1.0f;
};

// Pure -- real per-zone weight = 1 well inside the zone (distance <=
// radius*0.7), a real smoothstep falloff to 0 across the outer 30%, and
// exactly 0 beyond `radius`. The single highest-weight zone (if any)
// lerps `defaultSample` toward its own real fogColor/fogDensity/exposure
// by that weight; with no zones in range, returns `defaultSample`
// real-unchanged (verified by test) -- a live caller passing an empty
// `zones` vector (a map that hasn't built any) is a real, honest no-op.
[[nodiscard]] AtmosphereSample sampleAtmosphereZones(const std::vector<AtmosphereZone>& zones, glm::vec3 position,
                                                       const AtmosphereSample& defaultSample);

// Real, per-map zone builders -- see this file's own header comment for
// why these are zones layered on a caller-supplied baseline, not a
// standalone atmosphere system.

// Warmer, clearer air near each team's own real forge zone (the brief's
// own "warm near bases") -- everywhere else keeps the caller's own
// baseline (main.cpp's own existing cool, hazy Sky Map atmosphere --
// already real "cool near void" by construction, since that baseline was
// tuned for open sky between islands, not modified here).
[[nodiscard]] std::vector<AtmosphereZone> buildSkyMapAtmosphereZones(glm::vec3 teamABaseCenter,
                                                                       glm::vec3 teamBBaseCenter);

// Clearer, warmer, brighter air inside every major DerelictStation
// platform (the brief's own "station flicker panels" own real interior,
// pressurized-and-lit vs. open void) -- everywhere else keeps the
// caller's own baseline (a real, heavy void haze -- see Application's own
// live tick for that default).
[[nodiscard]] std::vector<AtmosphereZone> buildSpaceMapAtmosphereZones(const std::vector<SpacePlatform>& platforms);

} // namespace engine::tntwars
