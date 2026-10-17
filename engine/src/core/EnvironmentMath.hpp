#pragma once

#include <algorithm>
#include <cmath>

namespace engine::core {

// Fog amount 0..1: distance fog (exponential) times a height falloff above `baseHeight`.
inline float heightFogAmount(float distance, float height, float density, float baseHeight, float heightFalloff) {
    const float distanceFog = 1.0f - std::exp(-std::max(density, 0.0f) * std::max(distance, 0.0f));
    const float heightFactor = std::exp(-std::max(heightFalloff, 0.0f) * std::max(height - baseHeight, 0.0f));
    return std::clamp(distanceFog * heightFactor, 0.0f, 1.0f);
}

// Decaying camera shake: offset in -1..1 and an envelope that fades in/out.
struct ShakeEnvelope {
    float attack = 0.05f, sustain = 0.1f, release = 0.4f;
    [[nodiscard]] float duration() const { return attack + sustain + release; }
    [[nodiscard]] float at(float t) const {
        if (t < 0.0f || t >= duration()) return 0.0f;
        if (t < attack) return attack > 0.0f ? t / attack : 1.0f;
        if (t < attack + sustain) return 1.0f;
        return 1.0f - (t - attack - sustain) / release;
    }
};

// Deterministic shake without a random generator: sums sines at unrelated frequencies.
inline float shakeOffset(float t, float frequency, float seed) {
    return 0.6f * std::sin(t * frequency * 6.2831853f + seed) + 0.4f * std::sin(t * frequency * 1.618f * 6.2831853f + seed * 2.3f);
}

// Wind force on a particle: steady push plus gusts and a little turbulence from its position.
struct WindForce {
    float dirX = 1.0f, dirY = 0.0f, dirZ = 0.0f;
    float strength = 1.0f;
    float gustAmount = 0.5f, gustSpeed = 0.7f;
    float turbulence = 0.3f;

    void at(float px, float py, float pz, float time, float out[3]) const {
        const float gust = 1.0f + gustAmount * std::sin(time * gustSpeed * 6.2831853f + px * 0.2f);
        out[0] = dirX * strength * gust + turbulence * std::sin(py * 1.7f + time * 2.1f);
        out[1] = dirY * strength * gust + turbulence * std::sin(pz * 1.3f + time * 1.7f);
        out[2] = dirZ * strength * gust + turbulence * std::sin(px * 1.9f + time * 2.5f);
    }
};

// Folds 5.1/stereo/mono channel layouts down to stereo.
inline void downmixToStereo(const float* in, int channels, float& left, float& right) {
    if (channels <= 1) {
        left = right = in[0];
    } else if (channels == 2) {
        left = in[0];
        right = in[1];
    } else if (channels >= 6) { // L R C LFE Ls Rs
        left = in[0] + 0.7071f * in[2] + 0.7071f * in[4];
        right = in[1] + 0.7071f * in[2] + 0.7071f * in[5];
    } else {
        left = in[0];
        right = in[1];
    }
}

} // namespace engine::core
