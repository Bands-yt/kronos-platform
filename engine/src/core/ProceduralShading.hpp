#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace engine::core::shading {

inline float smoothstep(float a, float b, float x) {
    const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

inline float hashf(int x, int y, int z, uint32_t seed) {
    uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u + static_cast<uint32_t>(z) * 2246822519u + seed * 3266489917u;
    h = (h ^ (h >> 15)) * 2246822519u;
    h = (h ^ (h >> 13)) * 3266489917u;
    return static_cast<float>((h ^ (h >> 16)) & 0xFFFFFF) / 16777215.0f;
}

// Value noise 0..1 (218), smooth between lattice points.
inline float valueNoise(float x, float y, float z, uint32_t seed = 0) {
    const int xi = static_cast<int>(std::floor(x)), yi = static_cast<int>(std::floor(y)), zi = static_cast<int>(std::floor(z));
    const float fx = smoothstep(0, 1, x - xi), fy = smoothstep(0, 1, y - yi), fz = smoothstep(0, 1, z - zi);
    auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };
    float c[2][2][2];
    for (int dx = 0; dx < 2; ++dx)
        for (int dy = 0; dy < 2; ++dy)
            for (int dz = 0; dz < 2; ++dz) c[dx][dy][dz] = hashf(xi + dx, yi + dy, zi + dz, seed);
    return lerp(lerp(lerp(c[0][0][0], c[1][0][0], fx), lerp(c[0][1][0], c[1][1][0], fx), fy),
                lerp(lerp(c[0][0][1], c[1][0][1], fx), lerp(c[0][1][1], c[1][1][1], fx), fy), fz);
}

inline float fbm(float x, float y, float z, int octaves = 4, uint32_t seed = 0) {
    float sum = 0, amp = 0.5f, norm = 0;
    for (int i = 0; i < octaves; ++i) {
        sum += amp * valueNoise(x, y, z, seed + i);
        norm += amp;
        x *= 2;
        y *= 2;
        z *= 2;
        amp *= 0.5f;
    }
    return norm > 0 ? sum / norm : 0.0f;
}

// Linear gradient between two colours (218).
inline std::array<float, 3> gradient(const std::array<float, 3>& a, const std::array<float, 3>& b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return {a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t};
}

// Triplanar blend weights from a normal (209): sum to 1, sharpness > 1 tightens the blend.
inline std::array<float, 3> triplanarWeights(float nx, float ny, float nz, float sharpness = 4.0f) {
    std::array<float, 3> w{std::pow(std::fabs(nx), sharpness), std::pow(std::fabs(ny), sharpness), std::pow(std::fabs(nz), sharpness)};
    const float sum = w[0] + w[1] + w[2];
    if (sum <= 0) return {0, 1, 0};
    return {w[0] / sum, w[1] / sum, w[2] / sum};
}

// Height-based blend of two layers by vertex colour (210): the layer with the higher "height + weight" wins smoothly.
inline float heightBlend(float heightA, float heightB, float weightB, float softness = 0.1f) {
    const float a = heightA * (1.0f - weightB), b = heightB * weightB;
    return smoothstep(-softness, softness, b - a);
}

// Water (219): colour fades from shallow to deep with depth; edge foam where the water is very shallow.
struct WaterSample {
    float depthFade = 0; // 0 shallow .. 1 deep
    float foam = 0;
};

inline WaterSample waterAt(float depth, float maxDepth = 4.0f, float foamDepth = 0.25f) {
    return {std::clamp(depth / std::max(maxDepth, 1e-3f), 0.0f, 1.0f), 1.0f - smoothstep(0.0f, foamDepth, depth)};
}

// Toon shading (220): quantises lighting into `bands` steps with a soft edge.
inline float toonRamp(float nDotL, int bands = 3, float softEdge = 0.03f) {
    const float x = std::clamp(nDotL, 0.0f, 1.0f) * bands;
    const float band = std::floor(x);
    const float frac = x - band;
    const float edge = smoothstep(1.0f - softEdge * bands, 1.0f, frac);
    return std::min((band + edge) / bands, 1.0f);
}

// Tone mapping and exposure (265, 266).
inline float reinhard(float x) { return x / (1.0f + x); }

inline float acesFilm(float x) {
    const float a = 2.51f, b = 0.03f, c = 2.43f, d = 0.59f, e = 0.14f;
    return std::clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0f, 1.0f);
}

// Moves the exposure toward the target at `speed` per second (auto exposure). `avgLuminance` of the frame.
inline float adaptExposure(float current, float avgLuminance, float dt, float speed = 1.5f, float middleGrey = 0.18f,
                           float minExp = 0.25f, float maxExp = 4.0f) {
    const float target = std::clamp(middleGrey / std::max(avgLuminance, 1e-4f), minExp, maxExp);
    const float k = 1.0f - std::exp(-speed * std::max(dt, 0.0f));
    return current + (target - current) * k;
}

struct BloomPreset {
    const char* name;
    float threshold, intensity, radius;
};

inline std::array<BloomPreset, 3> bloomPresets() { return {{{"Off", 99.0f, 0.0f, 0.0f}, {"Subtle", 1.0f, 0.15f, 0.5f}, {"Dreamy", 0.7f, 0.4f, 1.0f}}}; }

// Render scale presets and dynamic resolution (244, 245).
struct RenderScalePreset {
    const char* name;
    float scale;
};

inline std::array<RenderScalePreset, 4> renderScalePresets() { return {{{"Native", 1.0f}, {"Quality", 0.77f}, {"Balanced", 0.67f}, {"Performance", 0.5f}}}; }

// Lowers the render scale when frames are slower than the target, raises it slowly when there is room.
inline float adjustRenderScale(float scale, float frameMs, float targetMs, float minScale = 0.5f) {
    if (frameMs > targetMs * 1.1f) return std::max(minScale, scale - 0.05f);
    if (frameMs < targetMs * 0.8f) return std::min(1.0f, scale + 0.02f);
    return scale;
}

} // namespace engine::core::shading
