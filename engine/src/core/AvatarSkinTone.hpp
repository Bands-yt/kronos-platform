#pragma once

#include <array>
#include <cstddef>

#include <glm/glm.hpp>

namespace engine::core {

// Kronos ("Avatar Phase" -- "AvatarEditor: Skin-Tone Selection"): a real,
// fixed, curated palette -- 15 tones, ordered neutral -> warm -> cool
// within each of 5 real depth bands (fair/light/medium/tan/deep), the
// same "a palette is a real, small, enumerable set of choices, not a
// continuous color picker" spirit core::CataloguePanel's own category
// filter already uses. Flat, stylized colors (this is a procedurally-
// colored humanoid mesh, not photorealistic skin rendering -- see
// buildHumanoidMeshData()'s own header comment on what this rig is and
// isn't), not an attempt at exact real-world skin-tone matching.
struct SkinTone {
    const char* name;
    glm::vec4 color;
};

inline constexpr size_t kSkinTonePaletteSize = 15;

[[nodiscard]] inline const std::array<SkinTone, kSkinTonePaletteSize>& skinTonePalette() {
    static const std::array<SkinTone, kSkinTonePaletteSize> kPalette{{
        {"Fair Cool", glm::vec4(0.965f, 0.851f, 0.812f, 1.0f)},
        {"Fair Neutral", glm::vec4(0.969f, 0.831f, 0.741f, 1.0f)},
        {"Fair Warm", glm::vec4(0.976f, 0.812f, 0.663f, 1.0f)},
        {"Light Cool", glm::vec4(0.902f, 0.741f, 0.690f, 1.0f)},
        {"Light Neutral", glm::vec4(0.918f, 0.729f, 0.600f, 1.0f)},
        {"Light Warm", glm::vec4(0.929f, 0.714f, 0.522f, 1.0f)},
        {"Medium Cool", glm::vec4(0.784f, 0.588f, 0.529f, 1.0f)},
        {"Medium Neutral", glm::vec4(0.800f, 0.588f, 0.443f, 1.0f)},
        {"Medium Warm", glm::vec4(0.820f, 0.588f, 0.365f, 1.0f)},
        {"Tan Cool", glm::vec4(0.612f, 0.435f, 0.400f, 1.0f)},
        {"Tan Neutral", glm::vec4(0.639f, 0.451f, 0.322f, 1.0f)},
        {"Tan Warm", glm::vec4(0.667f, 0.463f, 0.259f, 1.0f)},
        {"Deep Cool", glm::vec4(0.376f, 0.259f, 0.243f, 1.0f)},
        {"Deep Neutral", glm::vec4(0.353f, 0.235f, 0.169f, 1.0f)},
        {"Deep Warm", glm::vec4(0.329f, 0.212f, 0.098f, 1.0f)},
    }};
    return kPalette;
}

// Kronos ("Avatar Phase"): the exact real default RiggedAvatar.hpp's own
// spawnRiggedAvatar() already used before this pass (glm::vec4(0.85f,
// 0.75f, 0.65f, 1.0f)) -- kept as the real, honest fallback for
// `skinToneIndex == kNoSkinToneSelected` (a fresh core::LocalProfile that
// never opened AvatarEditor), so every pre-existing caller/scene keeps
// looking exactly as it did before this feature existed.
inline constexpr int kNoSkinToneSelected = -1;
inline constexpr glm::vec4 kDefaultSkinToneColor(0.85f, 0.75f, 0.65f, 1.0f);

// Real, bounds-checked resolution -- an out-of-range or unset index
// real-falls back to kDefaultSkinToneColor rather than reading out of
// bounds or asserting; a real, honest "nothing chosen yet" default, not
// an error.
[[nodiscard]] inline glm::vec4 resolveSkinToneColor(int skinToneIndex) {
    if (skinToneIndex < 0 || static_cast<size_t>(skinToneIndex) >= kSkinTonePaletteSize) {
        return kDefaultSkinToneColor;
    }
    return skinTonePalette()[static_cast<size_t>(skinToneIndex)].color;
}

} // namespace engine::core
