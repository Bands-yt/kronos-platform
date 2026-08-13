#pragma once

#include <glm/glm.hpp>

namespace engine::studio {

// Sprint 10 ("Creator Tools Phase 2"): the real material preset table --
// extracted out of MaterialPlugin.cpp (where it originally lived,
// private to that one plugin) so studio::plugins::CreatorAssetBrowserPlugin
// can offer the exact same real presets through its "Use" button, not a
// second, could-drift copy of the same five values.
struct MaterialPresetInfo {
    const char* label;
    glm::vec4 baseColor;
    float metallic;
    float roughness;
    glm::vec3 emissiveColor;
    float emissiveIntensity;
};

constexpr MaterialPresetInfo kMaterialPresets[] = {
    {"Stone", {0.5f, 0.49f, 0.46f, 1.0f}, 0.0f, 0.9f, {1.0f, 1.0f, 1.0f}, 0.0f},
    {"Metal", {0.72f, 0.73f, 0.75f, 1.0f}, 1.0f, 0.3f, {1.0f, 1.0f, 1.0f}, 0.0f},
    {"Crystal", {0.65f, 0.85f, 0.95f, 1.0f}, 0.1f, 0.05f, {0.5f, 0.85f, 1.0f}, 1.2f},
    {"Sand", {0.76f, 0.69f, 0.52f, 1.0f}, 0.0f, 0.95f, {1.0f, 1.0f, 1.0f}, 0.0f},
    {"Wood", {0.4f, 0.27f, 0.16f, 1.0f}, 0.0f, 0.75f, {1.0f, 1.0f, 1.0f}, 0.0f},
};
constexpr int kMaterialPresetCount = 5;

} // namespace engine::studio
