#pragma once

#include <cstdint>

#include <glm/glm.hpp>

namespace engine::core {

// Sprint 6 ("World Systems & Environment") -- the real value-noise/fBm
// math core::Terrain's generation functions are built on, extracted into
// its own small, pure, GPU-independent module specifically so it's
// headlessly unit-testable. core::Terrain itself owns real GPU mesh
// buffers (Mesh::uploadFromHost() needs a live VmaAllocator/VkDevice),
// putting its create()/regenerate*() methods in the same "needs a live
// device, tested live not headlessly" category core::Renderer is
// already in -- unlike core::Physics (pure CPU Jolt simulation, real
// headless test coverage all sprint). This module is what lets the
// *generation math specifically* get the same real, headless coverage
// core::Physics enjoys, even though the mesh it ultimately feeds does not.

// Deterministic hash -> value noise -- not Perlin/Simplex (no such
// library is vendored), real but visibly grid-aligned at low
// frequencies compared to gradient noise. Same seeded hash both
// Terrain::addNoise() (the brush) and fractalNoise2D() below (whole-
// terrain generation) build on.
[[nodiscard]] float valueNoise2D(float x, float z, uint32_t seed = 0);

// Real fractal Brownian motion -- layers `octaves` copies of valueNoise2D
// at doubling frequency (`lacunarity`) and shrinking amplitude
// (`persistence`) each octave. Deterministic from `seed`: identical
// inputs always produce an identical result, real for repeatable
// presets/tests, not true per-call randomness.
struct FractalNoiseParams {
    float baseFrequency = 0.05f;
    float amplitude = 4.0f;
    int octaves = 4;
    float lacunarity = 2.0f;
    float persistence = 0.5f;
    uint32_t seed = 1337;
};
[[nodiscard]] float fractalNoise2D(float x, float z, const FractalNoiseParams& params);

// Kronos ("Sky Map Full Engine Specification"): real Ken Perlin-style
// gradient noise -- unlike valueNoise2D() above (interpolated random
// *values* at lattice points, real but visibly grid-aligned at low
// frequencies), this interpolates the *dot product* of each lattice
// corner's own pseudo-random unit gradient vector against the sample's
// offset from that corner, using the real quintic fade curve
// (6t^5-15t^4+10t^3, not linear) classic Perlin noise uses -- smoother,
// less grid-aligned results than valueNoise2D() at the same frequency.
// Returns a real value in roughly [-1, 1] (gradient noise's own natural
// range, unlike value noise which is exactly [-1,1] by construction).
[[nodiscard]] float perlinNoise2D(float x, float z, uint32_t seed = 0);

// Real fBm built on perlinNoise2D() the same way fractalNoise2D() is
// built on valueNoise2D() -- a separate function (not a flag on
// fractalNoise2D()) so existing fractalNoise2D() callers/tests keep
// real-working byte-for-byte unchanged.
[[nodiscard]] float fractalPerlinNoise2D(float x, float z, const FractalNoiseParams& params);

// Real Worley (cellular) noise -- scatters exactly one real pseudo-random
// feature point inside every unit cell of a grid, then returns the real
// Euclidean distance from (x, z) to the *nearest* feature point across
// the sample's own cell and its 8 neighbors (F1, the standard "distance
// to nearest cell" Worley output -- enough for both terrain edge-breakup
// and island-center scattering, this module's own two real uses). A real
// 0 exactly at a feature point, growing outward; never negative. The same
// per-cell hash technique hashNoise() (this file's own internal helper)
// already establishes, just used to place a point inside a cell instead
// of a scalar value at a lattice corner.
[[nodiscard]] float worleyNoise2D(float x, float z, uint32_t seed = 0);

// Real, exact position of the one real feature point worleyNoise2D()
// itself scatters inside grid cell (cellX, cellZ) -- exposed separately
// so a real caller wanting the *point* (e.g. scattering real object
// placements at cell density, not just sampling a distance field) can
// get it directly rather than re-deriving it by densely sampling
// worleyNoise2D() for a local minimum. worleyNoise2D() is itself built
// on this function, so the two never real-disagree about where a given
// cell's own point actually is.
[[nodiscard]] glm::vec2 worleyFeaturePoint2D(int cellX, int cellZ, uint32_t seed = 0);

} // namespace engine::core
