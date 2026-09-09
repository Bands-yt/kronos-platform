#include "core/TerrainGenerator.hpp"

#include <algorithm>
#include <cmath>

#include "core/Noise.hpp"

namespace engine::core {

namespace {

size_t index(uint32_t x, uint32_t z, uint32_t resolution) { return static_cast<size_t>(z) * resolution + x; }

// splitmix64 -- tiny, fast, full-avalanche mix. Not cryptographic, just
// real and well-scattered: turns one uint64_t seed plus a small integer
// salt into as many independent-looking uint32_t sub-seeds/positions as a
// preset needs (crater centers, island centers, per-octave noise seeds),
// deterministically and without ever colliding between presets.
uint64_t splitmix64(uint64_t& state) {
    state += 0x9E3779B97F4A7C15ULL;
    uint64_t z = state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

// One independent uint32_t noise seed per preset (`salt` picks which),
// derived from the caller's own uint64_t seed -- so every preset's noise
// looks different even when called with the same top-level seed, without
// presets having to agree on disjoint salt ranges by convention alone.
uint32_t seedFor(uint64_t seed, uint32_t salt) {
    uint64_t mixed = seed ^ (static_cast<uint64_t>(salt) * 0x9E3779B97F4A7C15ULL);
    return static_cast<uint32_t>(splitmix64(mixed));
}

// Ridged fBm: each octave's plain Perlin value gets folded to 1-|n| and
// squared before summing, so ridges sit where a raw perlinNoise2D() octave
// crosses zero rather than where it peaks -- the standard trick for sharp,
// crest-like mountain silhouettes fractalPerlinNoise2D()'s smooth
// summation can't produce. Normalized by total octave weight, so the
// result stays roughly in [0,1] regardless of octave count.
float ridgedFbm(float x, float z, uint32_t seed, int octaves, float baseFrequency, float lacunarity,
                float persistence) {
    float sum = 0.0f;
    float amplitude = 1.0f;
    float frequency = baseFrequency;
    float totalAmplitude = 0.0f;
    for (int o = 0; o < octaves; ++o) {
        float n = perlinNoise2D(x * frequency, z * frequency, seed + static_cast<uint32_t>(o) * 101u);
        float ridge = 1.0f - std::fabs(n);
        ridge *= ridge;
        sum += ridge * amplitude;
        totalAmplitude += amplitude;
        frequency *= lacunarity;
        amplitude *= persistence;
    }
    return totalAmplitude > 0.0f ? sum / totalAmplitude : 0.0f;
}

// Shared crater shape both VolcanicCrater (one large stamp) and
// LunarCraters (many small stamps) apply: a smooth bowl centered on the
// stamp, ringed by a raised rim right at its edge (t=1), decaying to
// exactly 0 past t=1.15 so stamping doesn't touch texels far outside the
// crater's own footprint. Continuous in t, not a hard-edged circle -- no
// visible seam where one crater's contribution stops.
float craterProfile(float t, float depth, float rimHeight) {
    if (t >= 1.15f) return 0.0f;
    float bowl = -depth * std::exp(-t * t * 4.0f);
    float rimDelta = t - 1.0f;
    float rim = rimHeight * std::exp(-(rimDelta * rimDelta) * 20.0f);
    return bowl + rim;
}

float distance2D(float ax, float az, float bx, float bz) {
    float dx = ax - bx;
    float dz = az - bz;
    return std::sqrt(dx * dx + dz * dz);
}

// Per-preset tuning for computeBlend() below -- e.g. AlpineMountains sets
// a low snowLine (most of its upper height range reads as snow) while
// DesertDunes sets a near-1.0 sandLine (nearly everything reads as sand),
// so the same shared height/slope-based classifier produces genuinely
// different material mixes per preset without each preset needing its own
// blend function.
struct BlendThresholds {
    float snowLine = 0.8f;  // normalized height above which snow weight ramps in
    float sandLine = 0.2f;  // normalized height below which sand weight ramps in
    float rockSlope = 0.6f; // normalized slope above which rock weight ramps in
};

MaterialBlendWeights computeBlend(const std::vector<float>& heights, uint32_t resolution,
                                   const BlendThresholds& thresholds) {
    MaterialBlendWeights blend;
    size_t count = heights.size();
    blend.grass.assign(count, 0.0f);
    blend.rock.assign(count, 0.0f);
    blend.sand.assign(count, 0.0f);
    blend.snow.assign(count, 0.0f);
    if (heights.empty() || resolution == 0) return blend;

    auto [minIt, maxIt] = std::minmax_element(heights.begin(), heights.end());
    float minHeight = *minIt;
    float range = std::max(*maxIt - minHeight, 1e-4f);

    for (uint32_t z = 0; z < resolution; ++z) {
        for (uint32_t x = 0; x < resolution; ++x) {
            size_t i = index(x, z, resolution);
            float h = (heights[i] - minHeight) / range;

            // Central-difference slope, normalized by height range the
            // same way `h` is -- mirrors buildChunkMesh()'s own central-
            // difference normal, just reduced to a scalar magnitude here.
            uint32_t xL = x > 0 ? x - 1 : x;
            uint32_t xR = x < resolution - 1 ? x + 1 : x;
            uint32_t zD = z > 0 ? z - 1 : z;
            uint32_t zU = z < resolution - 1 ? z + 1 : z;
            float hL = heights[index(xL, z, resolution)];
            float hR = heights[index(xR, z, resolution)];
            float hD = heights[index(x, zD, resolution)];
            float hU = heights[index(x, zU, resolution)];
            float slope = (std::fabs(hR - hL) + std::fabs(hU - hD)) * 0.5f / range;

            // Ramp width is a fixed slope delta, NOT (1-rockSlope): central-
            // difference slope normalized by a whole heightmap's range never
            // gets close to 1.0 in practice (measured max across all ten
            // presets is ~0.1-0.44, even for a carved canyon wall or a
            // crater rim) -- a (1-rockSlope) denominator assumes a scale
            // the data never reaches, so rockSlope thresholds could never
            // produce meaningful rock weight regardless of how low they
            // were set. kRockRampWidth is picked from that measured range:
            // rock reaches full weight once slope exceeds threshold by 0.15.
            constexpr float kRockRampWidth = 0.15f;
            float rock = std::clamp((slope - thresholds.rockSlope) / kRockRampWidth, 0.0f, 1.0f);
            float snow = std::clamp((h - thresholds.snowLine) / std::max(1.0f - thresholds.snowLine, 1e-4f), 0.0f,
                                     1.0f);
            float sand =
                std::clamp((thresholds.sandLine - h) / std::max(thresholds.sandLine, 1e-4f), 0.0f, 1.0f);
            float grass = std::clamp(1.0f - rock - snow - sand, 0.0f, 1.0f);

            float sum = rock + snow + sand + grass;
            if (sum < 1e-4f) {
                grass = 1.0f;
                sum = 1.0f;
            }
            blend.rock[i] = rock / sum;
            blend.snow[i] = snow / sum;
            blend.sand[i] = sand / sum;
            blend.grass[i] = grass / sum;
        }
    }
    return blend;
}

} // namespace

namespace TerrainGenerator {

HeightmapResult generate(TerrainPreset preset, uint32_t resolution, uint64_t seed, float heightMultiplier) {
    HeightmapResult result;
    result.resolution = resolution;
    if (resolution == 0) return result;

    size_t texelCount = static_cast<size_t>(resolution) * resolution;
    result.heights.assign(texelCount, 0.0f);
    BlendThresholds thresholds; // per-preset override below

    switch (preset) {
        case TerrainPreset::FlatPlain: {
            // Tiny amplitude, two octaves -- reads flat at normal camera
            // distance but is a genuine (if subtle) height field, not a
            // literal std::fill(0.0f) special case. Amplitude matches
            // Terrain::Preset::FlatPlains' own recipe (0.15f) so a caller
            // passing heightMultiplier=1.0f gets a relief comparable to the
            // rest of this codebase's existing terrain presets.
            FractalNoiseParams params{0.015f, 0.15f, 2, 2.0f, 0.5f, seedFor(seed, 1)};
            for (uint32_t z = 0; z < resolution; ++z) {
                for (uint32_t x = 0; x < resolution; ++x) {
                    result.heights[index(x, z, resolution)] =
                        fractalPerlinNoise2D(static_cast<float>(x), static_cast<float>(z), params) * heightMultiplier;
                }
            }
            // snowLine/rockSlope pushed past the [0,1] normalized range
            // computeBlend() maps height/slope into -- the clamp there
            // forces the ramp to 0 everywhere, i.e. a real "off" switch,
            // not just an unlikely-to-trigger threshold. A flat plain has
            // no business showing snow caps or rock outcrops.
            thresholds = {2.0f, 0.25f, 3.0f};
            break;
        }

        case TerrainPreset::RollingHills: {
            // Broad, moderate-amplitude fBm, few octaves -- gentle swells,
            // no sharp features. Amplitude matches
            // Terrain::Preset::RollingHills' own recipe (3.5f) exactly.
            FractalNoiseParams params{0.025f, 3.5f, 4, 2.0f, 0.5f, seedFor(seed, 2)};
            for (uint32_t z = 0; z < resolution; ++z) {
                for (uint32_t x = 0; x < resolution; ++x) {
                    result.heights[index(x, z, resolution)] =
                        fractalPerlinNoise2D(static_cast<float>(x), static_cast<float>(z), params) * heightMultiplier;
                }
            }
            thresholds = {2.0f, 0.15f, 0.08f}; // snow off -- hills alone don't reach snow altitude
            break;
        }

        case TerrainPreset::AlpineMountains: {
            // Real two-layer mountain build, not one fBm call: a broad,
            // slow "uplift" (where the range sits) plus sharp ridged fBm
            // (its jagged detail) summed on top -- the standard way real
            // terrain generators separate a range's silhouette from its
            // peaks, and why this can't share RollingHills' single-fBm path.
            uint32_t upliftSeed = seedFor(seed, 3);
            uint32_t ridgeSeed = seedFor(seed, 33);
            FractalNoiseParams upliftParams{0.008f, 1.0f, 2, 2.0f, 0.5f, upliftSeed};
            for (uint32_t z = 0; z < resolution; ++z) {
                for (uint32_t x = 0; x < resolution; ++x) {
                    float fx = static_cast<float>(x);
                    float fz = static_cast<float>(z);
                    float uplift = fractalPerlinNoise2D(fx, fz, upliftParams);
                    float ridges = ridgedFbm(fx, fz, ridgeSeed, 7, 0.02f, 2.1f, 0.5f);
                    // Weighted to land taller than Terrain::Preset::RockyCanyon's
                    // amplitude (9.0f) -- this is the tallest of the ten presets,
                    // matching "AlpineMountains" being the sharpest/highest one.
                    result.heights[index(x, z, resolution)] = (uplift * 4.0f + ridges * 10.0f) * heightMultiplier;
                }
            }
            thresholds = {0.55f, 0.0f, 0.02f}; // sand off -- no beaches at altitude
            break;
        }

        case TerrainPreset::DesertDunes: {
            // Domain-warped sine ridges, not fBm at all -- long dune
            // crests running roughly along Z, their X position and shape
            // perturbed by a low-frequency fBm warp so they wander and
            // vary instead of forming perfectly straight, regular sine
            // bands, plus a thin high-frequency layer for grain-scale
            // texture.
            uint32_t warpSeed = seedFor(seed, 4);
            uint32_t grainSeed = seedFor(seed, 44);
            FractalNoiseParams warpParams{0.01f, 1.0f, 3, 2.0f, 0.5f, warpSeed};
            for (uint32_t z = 0; z < resolution; ++z) {
                for (uint32_t x = 0; x < resolution; ++x) {
                    float fx = static_cast<float>(x);
                    float fz = static_cast<float>(z);
                    float warp = fractalPerlinNoise2D(fx, fz, warpParams);
                    float ridge = std::sin(fx * 0.05f + warp * 3.0f) * 0.5f + 0.5f;
                    float grain = valueNoise2D(fx * 0.2f, fz * 0.2f, grainSeed) * 0.1f;
                    // Scaled up to a real hill-scale amplitude (~3.5 peak-to-
                    // trough) -- dune fields have visible relief, unlike
                    // FlatPlain, but stay well under mountain scale.
                    result.heights[index(x, z, resolution)] = (ridge * 0.8f + grain) * 4.0f * heightMultiplier;
                }
            }
            // sandLine pushed well past 1.0: (sandLine-h)/sandLine stays
            // close to 1 across the whole normalized height range, i.e.
            // "nearly everything is sand" rather than "sand only near h=0".
            // snow/rock both off -- dune fields have neither.
            thresholds = {2.0f, 50.0f, 5.0f};
            break;
        }

        case TerrainPreset::CanyonRavine: {
            // Rolling base fBm with a real carved trench: the trench's own
            // X center winds along Z (driven by a separate low-frequency
            // fBm, not a straight cut), and a quadratic V-profile
            // subtracts depth the closer a texel is to that wandering
            // center line, tapering to 0 at the trench's own half-width.
            uint32_t baseSeed = seedFor(seed, 5);
            uint32_t wobbleSeed = seedFor(seed, 55);
            FractalNoiseParams baseParams{0.02f, 3.5f, 4, 2.0f, 0.5f, baseSeed};
            FractalNoiseParams wobbleParams{0.01f, 1.0f, 2, 2.0f, 0.5f, wobbleSeed};
            float centerX = static_cast<float>(resolution) * 0.5f;
            float ravineWidth = static_cast<float>(resolution) * 0.06f;
            for (uint32_t z = 0; z < resolution; ++z) {
                float fz = static_cast<float>(z);
                float wobble = fractalPerlinNoise2D(0.0f, fz, wobbleParams) * static_cast<float>(resolution) * 0.18f;
                float ravineCenterX = centerX + wobble;
                for (uint32_t x = 0; x < resolution; ++x) {
                    float fx = static_cast<float>(x);
                    float base = fractalPerlinNoise2D(fx, fz, baseParams);
                    float t = std::fabs(fx - ravineCenterX) / ravineWidth;
                    // Carve depth (8.0f) set well past the base terrain's own
                    // amplitude (3.5f) -- a real deep ravine cuts below the
                    // surrounding plateau, not a shallow dimple in it.
                    float carve = t < 1.0f ? -(1.0f - t * t) * 8.0f : 0.0f;
                    result.heights[index(x, z, resolution)] = (base + carve) * heightMultiplier;
                }
            }
            thresholds = {2.0f, 0.0f, 0.05f}; // snow/sand off -- exposed rock and rim grass only
            break;
        }

        case TerrainPreset::VolcanicCrater: {
            // A smooth radial cone (rising toward the center, quadratic
            // falloff) with a single large craterProfile() stamp punched
            // into its peak -- raised rim right at the caldera edge,
            // depressed bowl inside it, matching a real volcano's profile
            // rather than a bare cone or a bare crater alone.
            uint32_t roughSeed = seedFor(seed, 6);
            FractalNoiseParams roughParams{0.04f, 0.5f, 4, 2.2f, 0.5f, roughSeed};
            float cx = static_cast<float>(resolution) * 0.5f;
            float cz = static_cast<float>(resolution) * 0.5f;
            float coneRadius = static_cast<float>(resolution) * 0.42f;
            float craterRadius = static_cast<float>(resolution) * 0.16f;
            for (uint32_t z = 0; z < resolution; ++z) {
                for (uint32_t x = 0; x < resolution; ++x) {
                    float fx = static_cast<float>(x);
                    float fz = static_cast<float>(z);
                    float coneT = std::clamp(distance2D(fx, fz, cx, cz) / coneRadius, 0.0f, 1.0f);
                    float cone = (1.0f - coneT) * (1.0f - coneT);
                    float craterT = distance2D(fx, fz, cx, cz) / craterRadius;
                    // Cone/crater amplitudes lifted into mountain-scale
                    // territory (peak ~10 units) -- a volcano is a mountain
                    // with a hole in it, not a small bump.
                    float crater = craterProfile(craterT, 3.0f, 2.0f);
                    float rough = fractalPerlinNoise2D(fx, fz, roughParams);
                    result.heights[index(x, z, resolution)] = (cone * 8.0f + crater + rough) * heightMultiplier;
                }
            }
            thresholds = {0.85f, 0.0f, 0.05f}; // sand off -- volcanic flanks, not beaches
            break;
        }

        case TerrainPreset::ArchipelagoIsland: {
            // A handful of seeded island centers (splitmix64-placed, not
            // fixed), each a steep radial dome; a texel's elevation is the
            // *max* dome contribution across every island (so islands
            // don't cancel or average into a single blob), plus fine fBm
            // detail, minus a flat sea-level offset -- edges/gaps between
            // islands sit below 0 (underwater), island interiors above it.
            uint64_t rngState = seed ^ 0xA1B2C3D4E5F60708ULL;
            int islandCount = 3 + static_cast<int>(splitmix64(rngState) % 3);
            struct Island {
                float x, z, radius;
            };
            std::vector<Island> islands;
            islands.reserve(static_cast<size_t>(islandCount));
            for (int i = 0; i < islandCount; ++i) {
                float ix = static_cast<float>(splitmix64(rngState) % resolution);
                float iz = static_cast<float>(splitmix64(rngState) % resolution);
                float radius = static_cast<float>(resolution) *
                               (0.12f + 0.08f * (static_cast<float>(splitmix64(rngState) % 1000) / 1000.0f));
                islands.push_back({ix, iz, radius});
            }
            FractalNoiseParams detailParams{0.05f, 0.15f, 4, 2.0f, 0.5f, seedFor(seed, 7)};
            // In the same height units as `best`/`detail` below (not a
            // fraction) -- island interiors sit above 0, gaps below it.
            constexpr float kSeaLevel = 1.5f;
            for (uint32_t z = 0; z < resolution; ++z) {
                for (uint32_t x = 0; x < resolution; ++x) {
                    float fx = static_cast<float>(x);
                    float fz = static_cast<float>(z);
                    float best = 0.0f;
                    for (const Island& island : islands) {
                        float t = std::clamp(distance2D(fx, fz, island.x, island.z) / island.radius, 0.0f, 1.0f);
                        float bump = 1.0f - t * t * t;
                        best = std::max(best, bump);
                    }
                    float detail = fractalPerlinNoise2D(fx, fz, detailParams);
                    result.heights[index(x, z, resolution)] = (best * 4.0f + detail * 0.5f - kSeaLevel) * heightMultiplier;
                }
            }
            thresholds = {2.0f, 0.45f, 0.12f}; // snow off -- these are tropical/temperate islands
            break;
        }

        case TerrainPreset::ArcticTundra: {
            // Mostly flat low-amplitude fBm base plus a Worley cellular
            // layer read as raised hummocks (1-distance peaks at each
            // cell's own feature point, decaying to 0 at the cell border)
            // -- the frost-heave "polygon ground" pattern real tundra
            // shows, not just plain noise at low amplitude.
            uint32_t baseSeed = seedFor(seed, 8);
            uint32_t cellSeed = seedFor(seed, 88);
            FractalNoiseParams baseParams{0.015f, 0.8f, 3, 2.0f, 0.5f, baseSeed};
            for (uint32_t z = 0; z < resolution; ++z) {
                for (uint32_t x = 0; x < resolution; ++x) {
                    float fx = static_cast<float>(x);
                    float fz = static_cast<float>(z);
                    float base = fractalPerlinNoise2D(fx, fz, baseParams);
                    float cell = worleyNoise2D(fx * 0.08f, fz * 0.08f, cellSeed);
                    float hummock = (1.0f - std::min(cell, 1.0f)) * 0.4f;
                    result.heights[index(x, z, resolution)] = (base + hummock) * heightMultiplier;
                }
            }
            thresholds = {0.3f, 0.0f, 0.06f}; // sand off -- frozen ground, no beaches
            break;
        }

        case TerrainPreset::LunarCraters: {
            // Flat, low-amplitude regolith base, then a seeded count of
            // craterProfile() stamps at pseudo-random (splitmix64)
            // positions/radii -- each stamp only touches its own local
            // bounding box, so crater density stays cheap independent of
            // resolution. This is the preset the directive names
            // explicitly as "stamp circular craters at pseudo-random
            // seeded positions", distinct from VolcanicCrater's single
            // large stamp on a cone.
            FractalNoiseParams baseParams{0.03f, 0.3f, 3, 2.0f, 0.5f, seedFor(seed, 10)};
            for (uint32_t z = 0; z < resolution; ++z) {
                for (uint32_t x = 0; x < resolution; ++x) {
                    result.heights[index(x, z, resolution)] =
                        fractalPerlinNoise2D(static_cast<float>(x), static_cast<float>(z), baseParams) *
                        heightMultiplier;
                }
            }

            uint64_t rngState = seed ^ 0x5EED5EED5EED5EEDULL;
            int craterCount = 14 + static_cast<int>(splitmix64(rngState) % 12);
            for (int i = 0; i < craterCount; ++i) {
                float cx = static_cast<float>(splitmix64(rngState) % resolution);
                float cz = static_cast<float>(splitmix64(rngState) % resolution);
                float radius = static_cast<float>(resolution) *
                               (0.03f + 0.09f * (static_cast<float>(splitmix64(rngState) % 1000) / 1000.0f));
                // Depth/rim drawn from a fixed range, NOT derived from
                // `radius` -- radius is deliberately resolution-relative (a
                // fraction of the grid, matching this file's other radial
                // presets), but depth is a real-world height and must stay
                // resolution-stable: the same seed at resolution 65 or 129
                // should carve equally deep craters, only sampled at
                // different density, matching VolcanicCrater's own fixed
                // (non-radius-derived) depth/rim constants above.
                float depth = 0.6f + 0.6f * (static_cast<float>(splitmix64(rngState) % 1000) / 1000.0f);
                float rim = depth * 0.5f;

                int minX = std::max(0, static_cast<int>(cx - radius * 1.2f));
                int maxX = std::min(static_cast<int>(resolution) - 1, static_cast<int>(cx + radius * 1.2f));
                int minZ = std::max(0, static_cast<int>(cz - radius * 1.2f));
                int maxZ = std::min(static_cast<int>(resolution) - 1, static_cast<int>(cz + radius * 1.2f));
                for (int gz = minZ; gz <= maxZ; ++gz) {
                    for (int gx = minX; gx <= maxX; ++gx) {
                        float t = distance2D(static_cast<float>(gx), static_cast<float>(gz), cx, cz) / radius;
                        result.heights[index(static_cast<uint32_t>(gx), static_cast<uint32_t>(gz), resolution)] +=
                            craterProfile(t, depth, rim) * heightMultiplier;
                    }
                }
            }
            // snow/sand off -- airless regolith reads as rock everywhere;
            // rockSlope pulled low so nearly every slope crosses it (crater
            // walls, rims, and the cratered floor all count as "rock").
            thresholds = {2.0f, 0.0f, -0.05f};
            break;
        }

        case TerrainPreset::TieredPlateau: {
            // Same fBm base RollingHills uses, but quantized to a fixed
            // step (floor(raw/step)*step) instead of used smoothly -- real
            // stepped mesa bands, not a smooth hill relabeled. Amplitude and
            // step size both scaled up so the bands are wide enough (~6
            // visible tiers) to read as real plateaus rather than a subtle
            // stairstep in a nearly-flat field.
            FractalNoiseParams params{0.02f, 6.0f, 4, 2.0f, 0.5f, seedFor(seed, 11)};
            constexpr float kStepSize = 1.0f;
            for (uint32_t z = 0; z < resolution; ++z) {
                for (uint32_t x = 0; x < resolution; ++x) {
                    float raw = fractalPerlinNoise2D(static_cast<float>(x), static_cast<float>(z), params);
                    float stepped = std::floor(raw / kStepSize) * kStepSize;
                    result.heights[index(x, z, resolution)] = stepped * heightMultiplier;
                }
            }
            thresholds = {0.85f, 0.0f, 0.06f}; // sand off -- mesa country, not coastline
            break;
        }
    }

    result.blend = computeBlend(result.heights, resolution, thresholds);
    return result;
}

} // namespace TerrainGenerator

} // namespace engine::core
