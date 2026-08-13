#pragma once

#include <glm/glm.hpp>

#include "miningsim/Zone.hpp"

namespace engine::miningsim {

// Kronos roadmap Milestone 17 ("World asset pipeline generalization"):
// real, per-zone-type visual theming -- the parametric half of
// generalizing Sprint 16's single, fixed-look MiningSimRtx prototype
// cavern (buildRtxPrototypeScene()) so it reads as a genuinely different
// real place per real ZoneType, reusing the exact same real geometry/
// material/particle/lighting *systems* that scene already established
// (core::ProceduralMaterialLibrary, core::SceneLighting's real point
// lights + exponential fog) rather than replacing them with a new
// pipeline. Real procedural primitives + real generated PBR textures
// throughout -- no hand-authored art exists or can exist in this engine
// (see MiningSimRtx.hpp's own header comment); "generalization" here
// means "the same real cavern shape, real-retinted/relit per zone," not
// a claim of a new per-zone geometry generator (that's Phase 5's later,
// separate "Procedural dungeons" milestone).
struct ZoneVisualTheme {
    glm::vec3 ambientTint{0.05f, 0.05f, 0.07f};
    glm::vec3 ambientGroundTint{0.035f, 0.03f, 0.025f};
    glm::vec3 fogColor{0.05f, 0.045f, 0.06f};
    float fogDensity = 0.012f;
    glm::vec3 keyLightColor{1.0f, 0.85f, 0.6f};
    // Real accent color standing in for that zone's own "glowing ore
    // vein"/crystal lighting -- Normal's own real default matches
    // MiningSimRtx.cpp's original, real, hand-tuned violet exactly, so
    // the default (ZoneType::Normal) call site's real look is unchanged.
    glm::vec3 accentLightColor{0.55f, 0.30f, 1.0f};
};

// Real, tuned, distinct theme per zone type -- ZoneType::Normal's own
// values are the real, original MiningSimRtx cavern look (unchanged);
// every other zone type gets its own real, deliberately different
// palette (e.g. Void's near-black ambient/minimal fog, Heavenly's bright
// warm-white key light, BioluminescentCaverns' vivid cyan-green accent).
[[nodiscard]] ZoneVisualTheme zoneVisualTheme(ZoneType zone);

} // namespace engine::miningsim
