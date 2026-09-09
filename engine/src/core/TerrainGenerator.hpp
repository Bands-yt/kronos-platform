#pragma once

#include <cstdint>
#include <vector>

namespace engine::core {

// Ten named whole-terrain recipes, all built on the same fBm/Perlin/Worley
// primitives Noise.hpp already provides (the same module
// Terrain::generateFractalTerrain()/applyPreset() use) -- a superset of
// Terrain::Preset's narrower RollingHills/FlatPlains/RockyCanyon, not a
// replacement: Terrain itself still owns brush edits, streaming, and the
// real GPU chunk meshes. This is a pure, headless generator; its output
// feeds Terrain via Terrain::restoreHeightSnapshot(), same as
// TerrainEditorPlugin's own undo/redo snapshots already do.
enum class TerrainPreset {
    FlatPlain,
    RollingHills,
    AlpineMountains,
    DesertDunes,
    CanyonRavine,
    VolcanicCrater,
    ArchipelagoIsland,
    ArcticTundra,
    LunarCraters,
    TieredPlateau,
};

// Real per-texel 0..1 material weights, one vector per channel, each sized
// resolution*resolution and indexed exactly like HeightmapResult::heights
// (index = z*resolution+x). Terrain.hpp's own material concept
// (HeightSlopeMaterialBand/applyHeightSlopeMaterialBlend) is chunk-
// granularity, not per-texel, so this deliberately doesn't reuse that
// type -- a caller wanting the coarser chunk-band behavior can still
// average each channel over a chunk's own texels to pick a band. The four
// channels are normalized to sum to 1 at every texel.
struct MaterialBlendWeights {
    std::vector<float> grass;
    std::vector<float> rock;
    std::vector<float> sand;
    std::vector<float> snow;
};

struct HeightmapResult {
    uint32_t resolution = 0;
    // resolution*resolution, row-major z-major (heights[z*resolution+x]) --
    // byte-identical layout/convention to Terrain::heights_, so
    // terrain.restoreHeightSnapshot(result.heights) is a valid, direct
    // call whenever resolution == terrain.info().gridResolution.
    std::vector<float> heights;
    MaterialBlendWeights blend;
};

// Stateless by design (no live device/allocator/ECS involved), the same
// headless-friendly shape as Noise.hpp's own functions -- see Noise.hpp's
// header comment for why that matters in this codebase.
namespace TerrainGenerator {

// Deterministic: identical (preset, resolution, seed, heightMultiplier)
// always produces byte-identical output. `seed` seeds every noise call and
// every pseudo-random feature placement (crater/island centers) this
// preset uses, not just one of them.
[[nodiscard]] HeightmapResult generate(TerrainPreset preset, uint32_t resolution, uint64_t seed,
                                        float heightMultiplier);

} // namespace TerrainGenerator

} // namespace engine::core
