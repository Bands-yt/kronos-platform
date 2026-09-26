#pragma once

#include <string>

#include <volk.h>
#include <vk_mem_alloc.h>

// Small Vulkan helpers shared by the self-contained render modules
// (IBL, clustered lighting, TAA). Deliberately free functions over raw
// handles so each module stays independent of Renderer.

namespace engine::core::render {

struct GpuContext {
    VkDevice device = VK_NULL_HANDLE;
    VmaAllocator allocator = nullptr;
    VkPipelineCache pipelineCache = VK_NULL_HANDLE;
    std::string shaderDir;
};

struct GpuImage {
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;
    VkImageView view = VK_NULL_HANDLE;
};

struct GpuBuffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;
    void* mapped = nullptr;
    VkDeviceSize size = 0;
};

[[nodiscard]] VkShaderModule loadShaderModule(const GpuContext& ctx, const char* spvName);

// Creates a compute pipeline from `spvName` (relative to ctx.shaderDir).
[[nodiscard]] VkPipeline createComputePipeline(const GpuContext& ctx, VkPipelineLayout layout, const char* spvName);

[[nodiscard]] bool createBuffer(const GpuContext& ctx, VkDeviceSize size, VkBufferUsageFlags usage, bool hostVisible,
                                GpuBuffer& out);
void destroyBuffer(const GpuContext& ctx, GpuBuffer& buffer);

// 2D, 2D array or cube (layers == 6 with cube = true) image with a full-range view.
[[nodiscard]] bool createImage(const GpuContext& ctx, VkFormat format, uint32_t width, uint32_t height,
                               uint32_t mipLevels, uint32_t layers, bool cube, VkImageUsageFlags usage, GpuImage& out,
                               VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT);
void destroyImage(const GpuContext& ctx, GpuImage& image);

[[nodiscard]] VkImageView createArrayView(const GpuContext& ctx, VkImage image, VkFormat format, uint32_t mip,
                                          uint32_t layers);

void imageBarrier(VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout,
                  VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess, VkPipelineStageFlags2 dstStage,
                  VkAccessFlags2 dstAccess, uint32_t baseMip = 0, uint32_t mipCount = VK_REMAINING_MIP_LEVELS,
                  uint32_t layers = VK_REMAINING_ARRAY_LAYERS,
                  VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT);

void bufferBarrier(VkCommandBuffer cmd, VkBuffer buffer, VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                   VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess);

[[nodiscard]] inline uint32_t divideRoundUp(uint32_t value, uint32_t divisor) {
    return (value + divisor - 1) / divisor;
}

} // namespace engine::core::render
