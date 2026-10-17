#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace engine::core {

// Per-layer weights (sum 1) from an RGBA splatmap pixel; up to 4 layers.
inline std::array<float, 4> splatWeights(uint8_t r, uint8_t g, uint8_t b, uint8_t a, int layers = 4) {
    std::array<float, 4> w{static_cast<float>(r), static_cast<float>(g), static_cast<float>(b), static_cast<float>(a)};
    for (int i = std::clamp(layers, 1, 4); i < 4; ++i) w[i] = 0.0f;
    const float sum = w[0] + w[1] + w[2] + w[3];
    if (sum <= 0.0f) return {1.0f, 0.0f, 0.0f, 0.0f};
    for (float& v : w) v /= sum;
    return w;
}

struct BiomeRule {
    std::string name;
    float minHeight = -1e9f, maxHeight = 1e9f;
    float maxSlopeDegrees = 90.0f;
    float minMoisture = 0.0f, maxMoisture = 1.0f;
};

// First matching rule wins; returns "" when none match.
inline std::string pickBiome(const std::vector<BiomeRule>& rules, float height, float slopeDegrees, float moisture) {
    for (const BiomeRule& r : rules) {
        if (height >= r.minHeight && height <= r.maxHeight && slopeDegrees <= r.maxSlopeDegrees &&
            moisture >= r.minMoisture && moisture <= r.maxMoisture) {
            return r.name;
        }
    }
    return {};
}

// 0 near the camera, 1 past `end`; smooth between, for blending terrain layers with distance.
inline float distanceBlend(float distance, float start, float end) {
    if (end <= start) return distance >= end ? 1.0f : 0.0f;
    const float t = std::clamp((distance - start) / (end - start), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

} // namespace engine::core
