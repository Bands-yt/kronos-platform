#include "core/GpuFeatures.hpp"

#include <algorithm>
#include <cstdio>

namespace engine::core {

namespace {

bool has(const std::vector<std::string>& extensions, const char* name) {
    return std::find(extensions.begin(), extensions.end(), name) != extensions.end();
}

void link(void* chainHead, void* feature) {
    auto* head = static_cast<VkBaseOutStructure*>(chainHead);
    auto* node = static_cast<VkBaseOutStructure*>(feature);
    node->pNext = head->pNext;
    head->pNext = node;
}

} // namespace

std::string GpuFeatureTier::summary() const {
    char text[256];
    std::snprintf(text, sizeof(text),
                  "Vulkan %u.%u | shading rate %s | host image copy %s | compute derivatives %s | shader clock %s",
                  VK_API_VERSION_MAJOR(apiVersion), VK_API_VERSION_MINOR(apiVersion),
                  fragmentShadingRate ? "yes" : "no", hostImageCopy ? "yes" : "no", computeDerivatives ? "yes" : "no",
                  shaderClock ? "yes" : "no");
    return text;
}

GpuFeatureTier selectGpuFeatures(const std::vector<std::string>& extensions, bool shadingRateFeature,
                                 bool hostImageCopyFeature, bool hostCopyRgba8, bool hostCopyToShaderRead,
                                 bool computeDerivativeFeature, bool shaderClockFeatures, uint32_t apiVersion) {
    GpuFeatureTier tier;
    tier.apiVersion = apiVersion;
    tier.fragmentShadingRate = has(extensions, VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME) && shadingRateFeature;
    // A host copy that can't target RGBA8 helps nothing we upload.
    tier.hostImageCopy = has(extensions, VK_EXT_HOST_IMAGE_COPY_EXTENSION_NAME) && hostImageCopyFeature && hostCopyRgba8;
    tier.hostCopyToShaderReadLayout = tier.hostImageCopy && hostCopyToShaderRead;
    const bool khrDerivatives = has(extensions, VK_KHR_COMPUTE_SHADER_DERIVATIVES_EXTENSION_NAME);
    const bool nvDerivatives = has(extensions, VK_NV_COMPUTE_SHADER_DERIVATIVES_EXTENSION_NAME);
    tier.computeDerivatives = (khrDerivatives || nvDerivatives) && computeDerivativeFeature;
    tier.computeDerivativesNv = tier.computeDerivatives && !khrDerivatives;
    tier.shaderClock = has(extensions, VK_KHR_SHADER_CLOCK_EXTENSION_NAME) && shaderClockFeatures;
    return tier;
}

GpuFeatureTier queryGpuFeatures(VkPhysicalDevice physicalDevice) {
    uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> properties(count);
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &count, properties.data());
    std::vector<std::string> extensions;
    for (const auto& property : properties) extensions.emplace_back(property.extensionName);

    VkPhysicalDeviceFragmentShadingRateFeaturesKHR shadingRate{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_FEATURES_KHR};
    VkPhysicalDeviceHostImageCopyFeaturesEXT hostImageCopy{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_IMAGE_COPY_FEATURES_EXT};
    VkPhysicalDeviceComputeShaderDerivativesFeaturesKHR derivatives{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COMPUTE_SHADER_DERIVATIVES_FEATURES_KHR};
    VkPhysicalDeviceShaderClockFeaturesKHR clock{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_CLOCK_FEATURES_KHR};
    VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    // Only chain structs whose extension exists; unknown sTypes are invalid usage.
    if (has(extensions, VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME)) link(&features, &shadingRate);
    if (has(extensions, VK_EXT_HOST_IMAGE_COPY_EXTENSION_NAME)) link(&features, &hostImageCopy);
    if (has(extensions, VK_KHR_COMPUTE_SHADER_DERIVATIVES_EXTENSION_NAME) ||
        has(extensions, VK_NV_COMPUTE_SHADER_DERIVATIVES_EXTENSION_NAME)) {
        link(&features, &derivatives); // the NV struct has the same layout and sType value
    }
    if (has(extensions, VK_KHR_SHADER_CLOCK_EXTENSION_NAME)) link(&features, &clock);
    vkGetPhysicalDeviceFeatures2(physicalDevice, &features);

    VkPhysicalDeviceProperties deviceProperties{};
    vkGetPhysicalDeviceProperties(physicalDevice, &deviceProperties);

    bool rgba8 = false;
    bool toShaderRead = false;
    if (has(extensions, VK_EXT_HOST_IMAGE_COPY_EXTENSION_NAME) && hostImageCopy.hostImageCopy) {
        rgba8 = true;
        for (VkFormat format : {VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_R8G8B8A8_UNORM}) {
            VkFormatProperties3 formatProperties3{VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3};
            VkFormatProperties2 formatProperties{VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2};
            formatProperties.pNext = &formatProperties3;
            vkGetPhysicalDeviceFormatProperties2(physicalDevice, format, &formatProperties);
            rgba8 = rgba8 && (formatProperties3.optimalTilingFeatures & VK_FORMAT_FEATURE_2_HOST_IMAGE_TRANSFER_BIT_EXT);

            // Skip it where host-copyable images would sample slower than normal ones.
            VkPhysicalDeviceImageFormatInfo2 imageInfo{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2};
            imageInfo.format = format;
            imageInfo.type = VK_IMAGE_TYPE_2D;
            imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_HOST_TRANSFER_BIT_EXT;
            VkHostImageCopyDevicePerformanceQueryEXT performance{
                VK_STRUCTURE_TYPE_HOST_IMAGE_COPY_DEVICE_PERFORMANCE_QUERY_EXT};
            VkImageFormatProperties2 imageProperties{VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2};
            imageProperties.pNext = &performance;
            rgba8 = rgba8 &&
                    vkGetPhysicalDeviceImageFormatProperties2(physicalDevice, &imageInfo, &imageProperties) == VK_SUCCESS &&
                    (performance.optimalDeviceAccess || performance.identicalMemoryLayout);
        }
        VkPhysicalDeviceHostImageCopyPropertiesEXT copyProperties{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_IMAGE_COPY_PROPERTIES_EXT};
        VkPhysicalDeviceProperties2 properties2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
        properties2.pNext = &copyProperties;
        vkGetPhysicalDeviceProperties2(physicalDevice, &properties2);
        std::vector<VkImageLayout> layouts(copyProperties.copyDstLayoutCount);
        copyProperties.pCopyDstLayouts = layouts.data();
        vkGetPhysicalDeviceProperties2(physicalDevice, &properties2);
        toShaderRead = std::find(layouts.begin(), layouts.end(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) != layouts.end();
    }

    return selectGpuFeatures(extensions, shadingRate.pipelineFragmentShadingRate == VK_TRUE,
                             hostImageCopy.hostImageCopy == VK_TRUE, rgba8, toShaderRead,
                             derivatives.computeDerivativeGroupQuads == VK_TRUE,
                             clock.shaderSubgroupClock == VK_TRUE && clock.shaderDeviceClock == VK_TRUE,
                             deviceProperties.apiVersion);
}

GpuFeatureEnables::GpuFeatureEnables(const GpuFeatureTier& tier) : tier_(tier) {}

void GpuFeatureEnables::apply(void* chainHead, std::vector<const char*>& extensions) {
    if (tier_.fragmentShadingRate) {
        shadingRate_.pipelineFragmentShadingRate = VK_TRUE;
        link(chainHead, &shadingRate_);
        extensions.push_back(VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME);
    }
    if (tier_.hostImageCopy) {
        hostImageCopy_.hostImageCopy = VK_TRUE;
        link(chainHead, &hostImageCopy_);
        extensions.push_back(VK_EXT_HOST_IMAGE_COPY_EXTENSION_NAME);
    }
    if (tier_.computeDerivatives) {
        computeDerivatives_.computeDerivativeGroupQuads = VK_TRUE;
        link(chainHead, &computeDerivatives_);
        extensions.push_back(tier_.computeDerivativesNv ? VK_NV_COMPUTE_SHADER_DERIVATIVES_EXTENSION_NAME
                                                        : VK_KHR_COMPUTE_SHADER_DERIVATIVES_EXTENSION_NAME);
    }
    if (tier_.shaderClock) {
        shaderClock_.shaderSubgroupClock = VK_TRUE;
        shaderClock_.shaderDeviceClock = VK_TRUE;
        link(chainHead, &shaderClock_);
        extensions.push_back(VK_KHR_SHADER_CLOCK_EXTENSION_NAME);
    }
}

} // namespace engine::core
