#pragma once

#include <array>
#include <vector>

#include <glm/glm.hpp>

#include "core/SceneTypes.hpp"
#include "core/render/GpuHelpers.hpp"

// Spot light shadow maps: one perspective depth map per shadowed spot, as
// layers of a per-frame 2D array. They are drawn by the same depth-only
// pipeline as the sun cascades and sampled from the clustered light loop
// through GpuLight::spotParams.w (the layer index).
//
// Each view shadows at most kMaxShadowedSpotLights spots: those that
// requested shadows and whose range touches the camera frustum, nearest
// first. The rest are lit unshadowed.

namespace engine::core::render {

inline constexpr uint32_t kSpotShadowResolution = 1024;

// Shadow frustums wider than this lose too much resolution to be useful;
// wider cones are only shadowed within it.
inline constexpr float kMaxSpotShadowHalfAngleDegrees = 80.0f;

struct SpotShadowSet {
    uint32_t count = 0;
    std::array<glm::mat4, kMaxShadowedSpotLights> viewProj{};
    glm::vec4 texelScale{0.0f}; // per slot: world size of one texel per metre from the light
};

struct SpotShadowMaps {
    GpuImage array; // view covers every layer
    std::array<VkImageView, kMaxShadowedSpotLights> layerViews{};
};

// `viewProj` must use [0, 1] clip depth.
[[nodiscard]] bool sphereInFrustum(const glm::mat4& viewProj, glm::vec3 center, float radius);

[[nodiscard]] glm::mat4 spotShadowViewProj(const GpuLight& light, float& texelScale);

// Replaces every kGpuLightShadowRequested marker with a map slot or kGpuLightNoShadow.
[[nodiscard]] SpotShadowSet assignSpotShadows(std::vector<GpuLight>& lights, const glm::mat4& cameraViewProj,
                                              glm::vec3 viewPosition);

[[nodiscard]] bool createSpotShadowMaps(const GpuContext& ctx, VkFormat depthFormat, SpotShadowMaps& out);
void destroySpotShadowMaps(const GpuContext& ctx, SpotShadowMaps& maps);

} // namespace engine::core::render
