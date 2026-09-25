#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "core/SceneTypes.hpp"
#include "core/render/GpuHelpers.hpp"

// Clustered forward (Forward+) light assignment.
//
// The view frustum is split into screen tiles x exponential depth slices.
// A compute pass writes, per cluster, a fixed-capacity list of the lights
// whose range sphere overlaps it; the forward shader then only evaluates
// the lights of the cluster its fragment falls in. Cost scales with local
// light density instead of total light count.
//
// Extension points: shadowed local lights (add a shadow index to
// GpuLight::spotParams.w), area lights (new type value), and a compacted
// variable-length index list if kMaxLightsPerCluster ever becomes limiting.

namespace engine::core {
class ECS;
}

namespace engine::core::render {

struct ClusterGrid {
    glm::uvec3 dims{1};
    float tileSizePixels = 1.0f;
    float sliceScale = 0.0f;
    float sliceBias = 0.0f;

    [[nodiscard]] uint32_t clusterCount() const { return dims.x * dims.y * dims.z; }
};

// Square tiles sized so the grid never exceeds kClusterTilesX x
// kClusterTilesY. Slices are logarithmic in view depth:
// slice = log(z) * scale - bias, so each slice spans the same depth ratio.
[[nodiscard]] ClusterGrid computeClusterGrid(uint32_t width, uint32_t height, float nearPlane, float farPlane);

[[nodiscard]] GpuLight makeGpuLight(glm::vec3 position, glm::vec3 color, float intensity, float range);
[[nodiscard]] GpuLight makeGpuSpotLight(glm::vec3 position, glm::vec3 direction, glm::vec3 color, float intensity,
                                        float range, float innerConeDegrees, float outerConeDegrees);

// Collects SceneLighting::pointLights plus every enabled Light entity. When
// more than kMaxGpuLights exist, the ones nearest `viewPosition` are kept.
void gatherLights(ECS& ecs, const SceneLighting& lighting, glm::vec3 viewPosition, std::vector<GpuLight>& out);

class ClusteredLighting {
public:
    struct FrameResources {
        GpuBuffer lights;
        GpuBuffer counts;
        GpuBuffer indices;
    };

    [[nodiscard]] bool initialize(const GpuContext& ctx, VkDescriptorSetLayout sceneSetLayout);
    void shutdown(const GpuContext& ctx);

    [[nodiscard]] static bool createFrameResources(const GpuContext& ctx, FrameResources& out);
    static void destroyFrameResources(const GpuContext& ctx, FrameResources& resources);

    // Returns the number of lights written (clamped to kMaxGpuLights).
    static uint32_t uploadLights(FrameResources& resources, const std::vector<GpuLight>& lights);

    // Records the cluster build. `sceneSet` must already reference this
    // frame's resources at bindings 8-10 and a UBO holding the grid.
    void record(VkCommandBuffer cmd, VkDescriptorSet sceneSet, const FrameResources& resources,
                const ClusterGrid& grid) const;

private:
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
};

} // namespace engine::core::render
