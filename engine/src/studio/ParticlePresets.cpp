#include "studio/ParticlePresets.hpp"

namespace engine::studio {

namespace {

void applyFirePreset(core::ParticleEmitterSettings& s) {
    s.looping = true;
    s.emissionRate = 40.0f;
    s.particleLifetime = 1.0f;
    s.particleLifetimeVariance = 0.3f;
    s.velocityMin = {-0.4f, 1.5f, -0.4f};
    s.velocityMax = {0.4f, 3.5f, 0.4f};
    s.gravity = {0.0f, 1.0f, 0.0f};
    s.sizeStart = 0.15f;
    s.sizeEnd = 0.02f;
    s.colorStart = {1.0f, 0.7f, 0.2f, 1.0f};
    s.colorEnd = {1.0f, 0.1f, 0.02f, 0.0f};
}

void applySmokePreset(core::ParticleEmitterSettings& s) {
    s.looping = true;
    s.emissionRate = 8.0f;
    s.particleLifetime = 3.0f;
    s.particleLifetimeVariance = 0.8f;
    s.velocityMin = {-0.2f, 0.6f, -0.2f};
    s.velocityMax = {0.2f, 1.2f, 0.2f};
    s.gravity = {0.0f, 0.3f, 0.0f};
    s.sizeStart = 0.2f;
    s.sizeEnd = 0.9f;
    s.colorStart = {0.6f, 0.6f, 0.6f, 0.5f};
    s.colorEnd = {0.3f, 0.3f, 0.3f, 0.0f};
}

void applySparklePreset(core::ParticleEmitterSettings& s) {
    s.looping = true;
    s.emissionRate = 25.0f;
    s.particleLifetime = 0.6f;
    s.particleLifetimeVariance = 0.2f;
    s.velocityMin = {-1.5f, -1.5f, -1.5f};
    s.velocityMax = {1.5f, 1.5f, 1.5f};
    s.gravity = {0.0f, -1.0f, 0.0f};
    s.sizeStart = 0.06f;
    s.sizeEnd = 0.0f;
    s.colorStart = {0.6f, 0.85f, 1.0f, 1.0f};
    s.colorEnd = {0.9f, 0.95f, 1.0f, 0.0f};
}

void applySnowPreset(core::ParticleEmitterSettings& s) {
    s.looping = true;
    s.emissionRate = 15.0f;
    s.particleLifetime = 5.0f;
    s.particleLifetimeVariance = 1.0f;
    s.velocityMin = {-0.3f, -0.6f, -0.3f};
    s.velocityMax = {0.3f, -0.3f, 0.3f};
    s.gravity = {0.0f, 0.0f, 0.0f};
    s.sizeStart = 0.05f;
    s.sizeEnd = 0.05f;
    s.colorStart = {1.0f, 1.0f, 1.0f, 0.9f};
    s.colorEnd = {1.0f, 1.0f, 1.0f, 0.0f};
}

void applyGlowPreset(core::ParticleEmitterSettings& s) {
    s.looping = true;
    s.emissionRate = 4.0f;
    s.particleLifetime = 2.5f;
    s.particleLifetimeVariance = 0.5f;
    s.velocityMin = {-0.05f, -0.05f, -0.05f};
    s.velocityMax = {0.05f, 0.05f, 0.05f};
    s.gravity = {0.0f, 0.0f, 0.0f};
    s.sizeStart = 0.35f;
    s.sizeEnd = 0.55f;
    s.colorStart = {0.5f, 0.85f, 1.0f, 0.55f};
    s.colorEnd = {0.5f, 0.85f, 1.0f, 0.0f};
}

void applyBurstPreset(core::ParticleEmitterSettings& s) {
    s.looping = false;
    s.emissionRate = 60.0f;
    s.particleLifetime = 0.7f;
    s.particleLifetimeVariance = 0.25f;
    s.velocityMin = {-3.0f, -3.0f, -3.0f};
    s.velocityMax = {3.0f, 3.0f, 3.0f};
    s.gravity = {0.0f, -4.0f, 0.0f};
    s.sizeStart = 0.1f;
    s.sizeEnd = 0.0f;
    s.colorStart = {1.0f, 0.85f, 0.4f, 1.0f};
    s.colorEnd = {1.0f, 0.3f, 0.1f, 0.0f};
}

} // namespace

const char* particlePresetName(ParticlePresetId preset) {
    switch (preset) {
        case ParticlePresetId::Fire: return "Fire";
        case ParticlePresetId::Smoke: return "Smoke";
        case ParticlePresetId::Sparkle: return "Sparkle (Spark)";
        case ParticlePresetId::Snow: return "Snow";
        case ParticlePresetId::Glow: return "Glow";
        case ParticlePresetId::Burst: return "Burst";
    }
    return "Unknown";
}

void applyParticlePreset(ParticlePresetId preset, core::ParticleEmitterSettings& settings) {
    switch (preset) {
        case ParticlePresetId::Fire: applyFirePreset(settings); break;
        case ParticlePresetId::Smoke: applySmokePreset(settings); break;
        case ParticlePresetId::Sparkle: applySparklePreset(settings); break;
        case ParticlePresetId::Snow: applySnowPreset(settings); break;
        case ParticlePresetId::Glow: applyGlowPreset(settings); break;
        case ParticlePresetId::Burst: applyBurstPreset(settings); break;
    }
}

} // namespace engine::studio
