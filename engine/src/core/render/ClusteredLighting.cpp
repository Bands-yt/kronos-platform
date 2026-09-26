#include "core/render/ClusteredLighting.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "core/Components.hpp"
#include "core/ECS.hpp"
#include "core/Hierarchy.hpp"

namespace engine::core::render {

namespace {

constexpr VkDeviceSize kLightBufferBytes = sizeof(GpuLight) * kMaxGpuLights;
constexpr uint32_t kMaxClusters = kClusterTilesX * kClusterTilesY * kClusterSlices;
constexpr VkDeviceSize kCountBufferBytes = sizeof(uint32_t) * kMaxClusters;
constexpr VkDeviceSize kIndexBufferBytes = sizeof(uint32_t) * kMaxClusters * kMaxLightsPerCluster;

} // namespace

ClusterGrid computeClusterGrid(uint32_t width, uint32_t height, float nearPlane, float farPlane) {
    ClusterGrid grid;
    width = std::max(width, 1u);
    height = std::max(height, 1u);
    float tile = std::ceil(std::max(static_cast<float>(width) / kClusterTilesX,
                                    static_cast<float>(height) / kClusterTilesY));
    tile = std::max(tile, 1.0f);
    grid.tileSizePixels = tile;
    grid.dims.x = std::min(divideRoundUp(width, static_cast<uint32_t>(tile)), kClusterTilesX);
    grid.dims.y = std::min(divideRoundUp(height, static_cast<uint32_t>(tile)), kClusterTilesY);
    grid.dims.z = kClusterSlices;

    float logRatio = std::log(farPlane / nearPlane);
    grid.sliceScale = static_cast<float>(kClusterSlices) / logRatio;
    grid.sliceBias = static_cast<float>(kClusterSlices) * std::log(nearPlane) / logRatio;
    return grid;
}

GpuLight makeGpuLight(glm::vec3 position, glm::vec3 color, float intensity, float range) {
    GpuLight light;
    light.positionRange = glm::vec4(position, std::max(range, 1e-3f));
    light.colorIntensity = glm::vec4(color, intensity);
    light.directionType = glm::vec4(0.0f, -1.0f, 0.0f, 0.0f);
    light.spotParams = glm::vec4(-1.0f, 1.0f, 1.0f, kGpuLightNoShadow);
    return light;
}

GpuLight makeGpuSpotLight(glm::vec3 position, glm::vec3 direction, glm::vec3 color, float intensity, float range,
                          float innerConeDegrees, float outerConeDegrees) {
    GpuLight light = makeGpuLight(position, color, intensity, range);
    float outer = glm::radians(std::clamp(outerConeDegrees, 0.5f, 89.5f));
    float inner = glm::radians(std::clamp(innerConeDegrees, 0.0f, glm::degrees(outer)));
    float cosOuter = std::cos(outer);
    float cosInner = std::cos(inner);
    light.directionType = glm::vec4(glm::normalize(direction), 1.0f);
    light.spotParams.x = cosOuter;
    light.spotParams.y = 1.0f / std::max(cosInner - cosOuter, 1e-4f);
    return light;
}

void gatherLights(ECS& ecs, const SceneLighting& lighting, glm::vec3 viewPosition, std::vector<GpuLight>& out) {
    out.clear();
    for (const SceneLighting::PointLight& light : lighting.pointLights) {
        out.push_back(makeGpuLight(light.position, light.color, light.intensity, light.radius));
    }

    auto view = ecs.view<Light, Transform>();
    for (auto entity : view) {
        const Light& light = view.get<Light>(entity);
        if (!light.enabled || light.intensity <= 0.0f) continue;
        glm::mat4 world = hierarchy::computeWorldMatrix(ecs, entity);
        glm::vec3 position(world[3]);
        if (light.type == LightType::Spot) {
            glm::vec3 direction = -glm::vec3(world[2]);
            if (glm::dot(direction, direction) < 1e-12f) direction = glm::vec3(0.0f, -1.0f, 0.0f);
            GpuLight spot = makeGpuSpotLight(position, direction, light.color, light.intensity, light.radius,
                                             light.innerConeDegrees, light.outerConeDegrees);
            if (light.castsShadow) spot.spotParams.w = kGpuLightShadowRequested;
            out.push_back(spot);
        } else {
            out.push_back(makeGpuLight(position, light.color, light.intensity, light.radius));
        }
    }

    if (out.size() > kMaxGpuLights) {
        // Rank by distance to the nearest point of the light's range sphere.
        auto key = [&](const GpuLight& l) {
            return glm::length(glm::vec3(l.positionRange) - viewPosition) - l.positionRange.w;
        };
        std::nth_element(out.begin(), out.begin() + kMaxGpuLights, out.end(),
                         [&](const GpuLight& a, const GpuLight& b) { return key(a) < key(b); });
        out.resize(kMaxGpuLights);
    }
}

bool ClusteredLighting::initialize(const GpuContext& ctx, VkDescriptorSetLayout sceneSetLayout) {
    VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &sceneSetLayout;
    if (vkCreatePipelineLayout(ctx.device, &layoutInfo, nullptr, &layout_) != VK_SUCCESS) return false;
    pipeline_ = createComputePipeline(ctx, layout_, "cluster_build.comp.spv");
    return pipeline_ != VK_NULL_HANDLE;
}

void ClusteredLighting::shutdown(const GpuContext& ctx) {
    if (pipeline_ != VK_NULL_HANDLE) vkDestroyPipeline(ctx.device, pipeline_, nullptr);
    if (layout_ != VK_NULL_HANDLE) vkDestroyPipelineLayout(ctx.device, layout_, nullptr);
    pipeline_ = VK_NULL_HANDLE;
    layout_ = VK_NULL_HANDLE;
}

bool ClusteredLighting::createFrameResources(const GpuContext& ctx, FrameResources& out) {
    return createBuffer(ctx, kLightBufferBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true, out.lights) &&
           createBuffer(ctx, kCountBufferBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false, out.counts) &&
           createBuffer(ctx, kIndexBufferBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false, out.indices);
}

void ClusteredLighting::destroyFrameResources(const GpuContext& ctx, FrameResources& resources) {
    destroyBuffer(ctx, resources.lights);
    destroyBuffer(ctx, resources.counts);
    destroyBuffer(ctx, resources.indices);
}

uint32_t ClusteredLighting::uploadLights(FrameResources& resources, const std::vector<GpuLight>& lights) {
    uint32_t count = static_cast<uint32_t>(std::min<size_t>(lights.size(), kMaxGpuLights));
    if (count > 0 && resources.lights.mapped != nullptr) {
        std::memcpy(resources.lights.mapped, lights.data(), sizeof(GpuLight) * count);
    }
    return count;
}

void ClusteredLighting::record(VkCommandBuffer cmd, VkDescriptorSet sceneSet, const FrameResources& resources,
                               const ClusterGrid& grid) const {
    if (pipeline_ == VK_NULL_HANDLE) return;

    // The same resources may have been read by an earlier draw of this
    // view in the same command buffer.
    for (VkBuffer buffer : {resources.counts.buffer, resources.indices.buffer}) {
        bufferBarrier(cmd, buffer, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
                      VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout_, 0, 1, &sceneSet, 0, nullptr);
    vkCmdDispatch(cmd, divideRoundUp(grid.clusterCount(), 128), 1, 1);

    for (VkBuffer buffer : {resources.counts.buffer, resources.indices.buffer}) {
        bufferBarrier(cmd, buffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                      VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
    }
}

} // namespace engine::core::render
