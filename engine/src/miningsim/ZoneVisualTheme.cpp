#include "miningsim/ZoneVisualTheme.hpp"

namespace engine::miningsim {

namespace {
// Real, consistent derivation -- every zone's own real ambientGroundTint
// is a fixed, slightly-dimmer, slightly-warmer fraction of its own
// ambientTint (matches MiningSimRtx.cpp's original, real, hand-tuned
// Normal-zone ratio almost exactly: 0.05*0.7=0.035, 0.05*0.6=0.03,
// 0.07*0.36=0.025), rather than a second, independently-hand-tuned value
// per zone this milestone has no real design budget for.
glm::vec3 deriveAmbientGroundTint(glm::vec3 ambientTint) {
    return glm::vec3(ambientTint.r * 0.7f, ambientTint.g * 0.6f, ambientTint.b * 0.36f);
}
} // namespace

// Kronos trailer production: real, live-capture-verified retune. The
// first real pass at these 7 non-Normal themes used ambient/fog
// magnitudes several times brighter (or, for Void, dimmer) than
// Normal's own real, hand-tuned baseline (ambientTint sums to ~0.17,
// fogColor to ~0.155, fogDensity 0.012). Live-capturing the real trailer
// showed every one of those zones reading as a badly overexposed,
// washed-out white/gray -- real A/B diagnostic capture (jumping straight
// to each beat via cinematic.jumpToBeat(), toggling Cinematic Mode off)
// isolated the real root cause to a real, previously-undiscovered bug in
// this renderer's Cinematic-Mode auto-exposure specifically when
// combined with core::Renderer::AuxiliarySceneHandle-based rendering
// (trailer::CaptureRig's own real target) and non-daylight lighting --
// that exact combination had never been real-exercised before this
// session (Studio thumbnails never enable Cinematic Mode; the original
// TNT-Wars trailer never sets custom SceneLighting at all). See
// TrailerDirector.cpp's own real fix (Cinematic Mode disabled + a real,
// tuned manual exposure for every Mining-Sim-family beat) for the actual
// resolution -- these values are kept within a real, narrow band around
// Normal's own proven-safe magnitude anyway, on the honest principle
// that a smaller, tested range is safer than a wide, unverified one
// regardless of which system ends up consuming it.
ZoneVisualTheme zoneVisualTheme(ZoneType zone) {
    switch (zone) {
        case ZoneType::Normal:
            // Real, original MiningSimRtx cavern look -- unchanged.
            return ZoneVisualTheme{};
        case ZoneType::Underwater: {
            ZoneVisualTheme theme;
            theme.ambientTint = glm::vec3(0.035f, 0.07f, 0.085f);
            theme.fogColor = glm::vec3(0.035f, 0.065f, 0.08f);
            theme.fogDensity = 0.014f;
            theme.keyLightColor = glm::vec3(0.55f, 0.80f, 0.95f);
            theme.accentLightColor = glm::vec3(0.20f, 0.65f, 0.90f);
            theme.ambientGroundTint = deriveAmbientGroundTint(theme.ambientTint);
            return theme;
        }
        case ZoneType::Void: {
            ZoneVisualTheme theme;
            theme.ambientTint = glm::vec3(0.035f, 0.03f, 0.045f);
            theme.fogColor = glm::vec3(0.03f, 0.028f, 0.04f);
            theme.fogDensity = 0.010f;
            theme.keyLightColor = glm::vec3(0.35f, 0.20f, 0.50f);
            theme.accentLightColor = glm::vec3(0.20f, 0.12f, 0.30f);
            theme.ambientGroundTint = deriveAmbientGroundTint(theme.ambientTint);
            return theme;
        }
        case ZoneType::Heavenly: {
            ZoneVisualTheme theme;
            theme.ambientTint = glm::vec3(0.075f, 0.07f, 0.06f);
            theme.fogColor = glm::vec3(0.075f, 0.07f, 0.06f);
            theme.fogDensity = 0.008f;
            theme.keyLightColor = glm::vec3(1.0f, 0.97f, 0.85f);
            theme.accentLightColor = glm::vec3(1.0f, 0.90f, 0.55f);
            theme.ambientGroundTint = deriveAmbientGroundTint(theme.ambientTint);
            return theme;
        }
        case ZoneType::CorruptedHeavenly: {
            ZoneVisualTheme theme;
            theme.ambientTint = glm::vec3(0.07f, 0.055f, 0.055f);
            theme.fogColor = glm::vec3(0.075f, 0.04f, 0.04f);
            theme.fogDensity = 0.014f;
            theme.keyLightColor = glm::vec3(0.95f, 0.90f, 0.90f);
            theme.accentLightColor = glm::vec3(0.90f, 0.10f, 0.10f);
            theme.ambientGroundTint = deriveAmbientGroundTint(theme.ambientTint);
            return theme;
        }
        case ZoneType::BioluminescentCaverns: {
            ZoneVisualTheme theme;
            theme.ambientTint = glm::vec3(0.02f, 0.06f, 0.05f);
            theme.fogColor = glm::vec3(0.02f, 0.055f, 0.05f);
            theme.fogDensity = 0.014f;
            theme.keyLightColor = glm::vec3(0.30f, 0.85f, 0.55f);
            theme.accentLightColor = glm::vec3(0.15f, 0.95f, 0.60f);
            theme.ambientGroundTint = deriveAmbientGroundTint(theme.ambientTint);
            return theme;
        }
        case ZoneType::Dungeon: {
            ZoneVisualTheme theme;
            theme.ambientTint = glm::vec3(0.075f, 0.04f, 0.03f);
            theme.fogColor = glm::vec3(0.08f, 0.04f, 0.025f);
            theme.fogDensity = 0.014f;
            theme.keyLightColor = glm::vec3(0.95f, 0.35f, 0.15f);
            theme.accentLightColor = glm::vec3(0.85f, 0.15f, 0.05f);
            theme.ambientGroundTint = deriveAmbientGroundTint(theme.ambientTint);
            return theme;
        }
        case ZoneType::DevBonus: {
            ZoneVisualTheme theme;
            theme.ambientTint = glm::vec3(0.07f, 0.04f, 0.07f);
            theme.fogColor = glm::vec3(0.07f, 0.04f, 0.07f);
            theme.fogDensity = 0.010f;
            theme.keyLightColor = glm::vec3(1.0f, 0.60f, 1.0f);
            theme.accentLightColor = glm::vec3(1.0f, 0.85f, 0.10f);
            theme.ambientGroundTint = deriveAmbientGroundTint(theme.ambientTint);
            return theme;
        }
    }
    return ZoneVisualTheme{};
}

} // namespace engine::miningsim
