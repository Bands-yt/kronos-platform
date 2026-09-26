#include "core/render/SpotShadows.hpp"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

namespace engine::core::render {

bool sphereInFrustum(const glm::mat4& viewProj, glm::vec3 center, float radius) {
    // Gribb-Hartmann: each plane is a combination of the matrix rows; near is row 2 alone for [0, 1] depth.
    const glm::mat4 m = glm::transpose(viewProj);
    const glm::vec4 planes[6] = {m[3] + m[0], m[3] - m[0], m[3] + m[1], m[3] - m[1], m[2], m[3] - m[2]};
    for (const glm::vec4& plane : planes) {
        float length = glm::length(glm::vec3(plane));
        if (length <= 0.0f) continue;
        if ((glm::dot(glm::vec3(plane), center) + plane.w) / length < -radius) return false;
    }
    return true;
}

glm::mat4 spotShadowViewProj(const GpuLight& light, float& texelScale) {
    const glm::vec3 position(light.positionRange);
    const glm::vec3 direction = glm::normalize(glm::vec3(light.directionType));
    const float range = light.positionRange.w;

    const float minCos = std::cos(glm::radians(kMaxSpotShadowHalfAngleDegrees));
    const float cosOuter = std::clamp(light.spotParams.x, minCos, 1.0f);
    // Widened by a few texels so filter taps at the cone edge stay inside the map.
    const float tanHalf = std::sqrt(std::max(1.0f - cosOuter * cosOuter, 1e-8f)) / cosOuter *
                          (1.0f + 4.0f / static_cast<float>(kSpotShadowResolution));

    const glm::vec3 up = std::abs(direction.y) > 0.99f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
    const glm::mat4 view = glm::lookAt(position, position + direction, up);
    const float nearPlane = std::clamp(range * 0.005f, 0.02f, 0.5f);
    const glm::mat4 proj = glm::perspective(2.0f * std::atan(tanHalf), 1.0f, nearPlane, range);

    texelScale = 2.0f * tanHalf / static_cast<float>(kSpotShadowResolution);
    return proj * view;
}

SpotShadowSet assignSpotShadows(std::vector<GpuLight>& lights, const glm::mat4& cameraViewProj,
                                glm::vec3 viewPosition) {
    struct Candidate {
        uint32_t index;
        float distance;
    };
    std::vector<Candidate> candidates;
    for (uint32_t i = 0; i < lights.size(); ++i) {
        GpuLight& light = lights[i];
        if (light.spotParams.w != kGpuLightShadowRequested) continue;
        light.spotParams.w = kGpuLightNoShadow;
        const glm::vec3 position(light.positionRange);
        const float range = light.positionRange.w;
        if (!sphereInFrustum(cameraViewProj, position, range)) continue;
        candidates.push_back({i, glm::length(position - viewPosition) - range});
    }
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        return a.distance != b.distance ? a.distance < b.distance : a.index < b.index;
    });

    SpotShadowSet set;
    for (const Candidate& candidate : candidates) {
        if (set.count == kMaxShadowedSpotLights) break;
        const uint32_t slot = set.count++;
        GpuLight& light = lights[candidate.index];
        light.spotParams.w = static_cast<float>(slot);
        set.viewProj[slot] = spotShadowViewProj(light, set.texelScale[slot]);
    }
    return set;
}

bool createSpotShadowMaps(const GpuContext& ctx, VkFormat depthFormat, SpotShadowMaps& out) {
    if (!createImage(ctx, depthFormat, kSpotShadowResolution, kSpotShadowResolution, 1, kMaxShadowedSpotLights, false,
                     VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, out.array,
                     VK_IMAGE_ASPECT_DEPTH_BIT)) {
        return false;
    }
    for (uint32_t layer = 0; layer < kMaxShadowedSpotLights; ++layer) {
        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image = out.array.image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = depthFormat;
        viewInfo.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, layer, 1};
        if (vkCreateImageView(ctx.device, &viewInfo, nullptr, &out.layerViews[layer]) != VK_SUCCESS) {
            destroySpotShadowMaps(ctx, out);
            return false;
        }
    }
    return true;
}

void destroySpotShadowMaps(const GpuContext& ctx, SpotShadowMaps& maps) {
    for (VkImageView& view : maps.layerViews) {
        if (view != VK_NULL_HANDLE) vkDestroyImageView(ctx.device, view, nullptr);
        view = VK_NULL_HANDLE;
    }
    destroyImage(ctx, maps.array);
}

} // namespace engine::core::render
