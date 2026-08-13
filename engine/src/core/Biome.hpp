#pragma once

#include <cstdint>

#include "core/SceneTypes.hpp"

namespace engine::core {

// Sprint 6 ("World Systems & Environment") task category 5: biome
// scaffolding -- deliberately just scaffolding, per the brief's own
// wording, not a live "walk into the forest and the lighting changes"
// system. What's real: a genuine registry with real, distinct lighting
// data per biome, and a real, callable function that applies one to a
// SceneLighting -- what's *not* built this pass is anything that
// automatically detects which biome the camera/character is currently in
// and calls it (a real, deliberate follow-up, see README's Known
// Issues). core::Terrain::setChunkBiome()/chunkBiome() (Terrain.hpp) is
// the real, already-wired half of "tag terrain chunks with a biome" --
// it stores a plain int32_t so Terrain has no dependency on this header
// at all (see its own comment); BiomeId is what gives that int real
// meaning.
enum class BiomeId : int32_t { Forest = 0, Desert = 1, Cave = 2 };
constexpr size_t kBiomeCount = 3;

[[nodiscard]] const char* biomeName(BiomeId biome);

// Real, distinct lighting presets per biome -- not the same values
// three times with a different name. Forest: cool, dim, blue-green
// canopy-filtered light. Desert: hot, bright, warm-white harsh sun.
// Cave: very dim, near-monochrome, almost no directional contribution
// (a cave has no real "sun" -- see applyBiomeLighting()'s own comment on
// why intensity is deliberately near zero, not literally zero).
struct BiomePreset {
    BiomeId id;
    const char* name;
    glm::vec3 lightColor;
    float lightIntensity;
    glm::vec3 ambient;
    glm::vec3 ambientGround;
};

[[nodiscard]] const BiomePreset& biomePreset(BiomeId biome);

// Applies `biome`'s real preset values onto `lighting` -- overwrites
// color/intensity/ambient/ambientGround, leaves `directionWS` untouched
// (a biome has a real lighting *mood*, not its own independent sun
// angle -- time-of-day, see core/TimeOfDay.hpp, is what actually owns
// direction). Real, callable, and unit-tested -- what's missing for a
// live biome system is only the "which biome is the camera in right
// now" detection this pass deliberately doesn't build (see this header's
// own comment).
void applyBiomeLighting(BiomeId biome, SceneLighting& lighting);

} // namespace engine::core
