#include "core/Noise.hpp"

#include <cmath>

#include <glm/glm.hpp>

namespace engine::core {

namespace {
float hashNoise(int x, int z, uint32_t seed) {
    uint32_t h = static_cast<uint32_t>(x * 374761393 + z * 668265263) ^ (seed * 2654435761u);
    h = (h ^ (h >> 13)) * 1274126177u;
    h = h ^ (h >> 16);
    return static_cast<float>(h) / static_cast<float>(0xFFFFFFFFu);
}
} // namespace

float valueNoise2D(float x, float z, uint32_t seed) {
    int x0 = static_cast<int>(std::floor(x));
    int z0 = static_cast<int>(std::floor(z));
    float fx = x - static_cast<float>(x0);
    float fz = z - static_cast<float>(z0);

    float v00 = hashNoise(x0, z0, seed);
    float v10 = hashNoise(x0 + 1, z0, seed);
    float v01 = hashNoise(x0, z0 + 1, seed);
    float v11 = hashNoise(x0 + 1, z0 + 1, seed);

    float ix0 = glm::mix(v00, v10, fx);
    float ix1 = glm::mix(v01, v11, fx);
    return glm::mix(ix0, ix1, fz) * 2.0f - 1.0f; // remap 0..1 -> -1..1
}

float fractalNoise2D(float x, float z, const FractalNoiseParams& params) {
    float amplitude = params.amplitude;
    float frequency = params.baseFrequency;
    float sum = 0.0f;
    for (int octave = 0; octave < params.octaves; ++octave) {
        sum += valueNoise2D(x * frequency, z * frequency, params.seed + static_cast<uint32_t>(octave) * 101u) * amplitude;
        amplitude *= params.persistence;
        frequency *= params.lacunarity;
    }
    return sum;
}

namespace {
// Real, deterministic unit gradient vector at lattice corner (x, z) --
// hashNoise() already returns a real, well-distributed [0,1) float from
// (x, z, seed), so it doubles as a real angle source here (angle =
// hash * 2*PI) rather than needing a second, separate hash table the
// classic Perlin reference implementation uses.
glm::vec2 perlinGradient(int x, int z, uint32_t seed) {
    float angle = hashNoise(x, z, seed) * 6.28318530718f;
    return glm::vec2(std::cos(angle), std::sin(angle));
}

// Real Perlin quintic fade curve -- 6t^5 - 15t^4 + 10t^3. Zero first and
// second derivative at both t=0 and t=1 (unlike a raw linear or even a
// cubic smoothstep), the real reason gradient noise built on it reads as
// smoother/less grid-aligned than valueNoise2D()'s own linear
// interpolation at the same frequency.
float perlinFade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }
} // namespace

float perlinNoise2D(float x, float z, uint32_t seed) {
    int x0 = static_cast<int>(std::floor(x));
    int z0 = static_cast<int>(std::floor(z));
    float fx = x - static_cast<float>(x0);
    float fz = z - static_cast<float>(z0);

    glm::vec2 g00 = perlinGradient(x0, z0, seed);
    glm::vec2 g10 = perlinGradient(x0 + 1, z0, seed);
    glm::vec2 g01 = perlinGradient(x0, z0 + 1, seed);
    glm::vec2 g11 = perlinGradient(x0 + 1, z0 + 1, seed);

    float d00 = glm::dot(g00, glm::vec2(fx, fz));
    float d10 = glm::dot(g10, glm::vec2(fx - 1.0f, fz));
    float d01 = glm::dot(g01, glm::vec2(fx, fz - 1.0f));
    float d11 = glm::dot(g11, glm::vec2(fx - 1.0f, fz - 1.0f));

    float u = perlinFade(fx);
    float v = perlinFade(fz);
    float ix0 = glm::mix(d00, d10, u);
    float ix1 = glm::mix(d01, d11, u);
    return glm::mix(ix0, ix1, v);
}

float fractalPerlinNoise2D(float x, float z, const FractalNoiseParams& params) {
    float amplitude = params.amplitude;
    float frequency = params.baseFrequency;
    float sum = 0.0f;
    for (int octave = 0; octave < params.octaves; ++octave) {
        sum += perlinNoise2D(x * frequency, z * frequency, params.seed + static_cast<uint32_t>(octave) * 101u) * amplitude;
        amplitude *= params.persistence;
        frequency *= params.lacunarity;
    }
    return sum;
}

glm::vec2 worleyFeaturePoint2D(int cellX, int cellZ, uint32_t seed) {
    // Two independent hashes (seed and seed+1) so the feature point's X
    // and Z offsets don't real-correlate (using the same hash for both
    // would always place the point on the cell's own diagonal).
    float offsetX = hashNoise(cellX, cellZ, seed);
    float offsetZ = hashNoise(cellX, cellZ, seed + 1u);
    return glm::vec2(static_cast<float>(cellX) + offsetX, static_cast<float>(cellZ) + offsetZ);
}

float worleyNoise2D(float x, float z, uint32_t seed) {
    int cellX = static_cast<int>(std::floor(x));
    int cellZ = static_cast<int>(std::floor(z));

    float nearestDistSq = 1e9f;
    // Real 3x3 neighborhood -- a feature point in an adjacent cell can
    // still be the real nearest one to a sample point near this cell's
    // own edge (the standard Worley-noise correctness requirement: a
    // single cell's own feature point alone isn't enough).
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            glm::vec2 feature = worleyFeaturePoint2D(cellX + dx, cellZ + dz, seed);
            float ddx = x - feature.x;
            float ddz = z - feature.y;
            float distSq = ddx * ddx + ddz * ddz;
            if (distSq < nearestDistSq) nearestDistSq = distSq;
        }
    }
    return std::sqrt(nearestDistSq);
}

} // namespace engine::core
