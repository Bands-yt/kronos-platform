#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <volk.h>

namespace engine::core {

// Optional Vulkan 1.4-era features. Every one has a fallback, so a GPU
// without any of them runs exactly as before.
struct GpuFeatureTier {
    uint32_t apiVersion = 0;
    bool fragmentShadingRate = false;   // VK_KHR_fragment_shading_rate (pipeline + per-draw rate)
    bool hostImageCopy = false;         // VK_EXT_host_image_copy, usable for RGBA8 textures
    bool computeDerivatives = false;    // VK_KHR/NV_compute_shader_derivatives (quad layout)
    bool shaderClock = false;           // VK_KHR_shader_clock (subgroup + device clocks)
    bool computeDerivativesNv = false;  // only the NV extension is present
    // Layouts vkCopyMemoryToImageEXT may write in; SHADER_READ_ONLY_OPTIMAL lets uploads skip a transition.
    bool hostCopyToShaderReadLayout = false;

    [[nodiscard]] std::string summary() const;
};

// Pure function so tests can check the selection without a GPU.
GpuFeatureTier selectGpuFeatures(const std::vector<std::string>& extensions, bool shadingRateFeature,
                                 bool hostImageCopyFeature, bool hostCopyRgba8, bool hostCopyToShaderRead,
                                 bool computeDerivativeFeature, bool shaderClockFeatures, uint32_t apiVersion);

GpuFeatureTier queryGpuFeatures(VkPhysicalDevice physicalDevice);

// Feature structs chained into VkDeviceCreateInfo for whatever `tier` found.
class GpuFeatureEnables {
public:
    explicit GpuFeatureEnables(const GpuFeatureTier& tier);
    GpuFeatureEnables(const GpuFeatureEnables&) = delete;
    GpuFeatureEnables& operator=(const GpuFeatureEnables&) = delete;

    // Prepends the needed structs to `chainHead`'s pNext and appends the extension names.
    void apply(void* chainHead, std::vector<const char*>& extensions);

private:
    GpuFeatureTier tier_;
    VkPhysicalDeviceFragmentShadingRateFeaturesKHR shadingRate_{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_FEATURES_KHR};
    VkPhysicalDeviceHostImageCopyFeaturesEXT hostImageCopy_{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_IMAGE_COPY_FEATURES_EXT};
    VkPhysicalDeviceComputeShaderDerivativesFeaturesKHR computeDerivatives_{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COMPUTE_SHADER_DERIVATIVES_FEATURES_KHR};
    VkPhysicalDeviceShaderClockFeaturesKHR shaderClock_{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_CLOCK_FEATURES_KHR};
};

} // namespace engine::core
