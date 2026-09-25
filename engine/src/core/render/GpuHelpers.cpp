#include "core/render/GpuHelpers.hpp"

#include <cstdio>
#include <fstream>
#include <vector>

namespace engine::core::render {

VkShaderModule loadShaderModule(const GpuContext& ctx, const char* spvName) {
    std::string path = ctx.shaderDir + "/" + spvName;
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        std::fprintf(stderr, "render: could not open shader \"%s\".\n", path.c_str());
        return VK_NULL_HANDLE;
    }
    std::vector<char> code(static_cast<size_t>(file.tellg()));
    file.seekg(0);
    file.read(code.data(), static_cast<std::streamsize>(code.size()));
    if (code.empty() || code.size() % 4 != 0) return VK_NULL_HANDLE;

    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = code.size();
    info.pCode = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(ctx.device, &info, nullptr, &module) != VK_SUCCESS) return VK_NULL_HANDLE;
    return module;
}

VkPipeline createComputePipeline(const GpuContext& ctx, VkPipelineLayout layout, const char* spvName) {
    VkShaderModule module = loadShaderModule(ctx, spvName);
    if (module == VK_NULL_HANDLE) return VK_NULL_HANDLE;

    VkComputePipelineCreateInfo info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    info.stage = VkPipelineShaderStageCreateInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    info.stage.module = module;
    info.stage.pName = "main";
    info.layout = layout;
    VkPipeline pipeline = VK_NULL_HANDLE;
    if (vkCreateComputePipelines(ctx.device, ctx.pipelineCache, 1, &info, nullptr, &pipeline) != VK_SUCCESS) {
        std::fprintf(stderr, "render: compute pipeline \"%s\" failed.\n", spvName);
        pipeline = VK_NULL_HANDLE;
    }
    vkDestroyShaderModule(ctx.device, module, nullptr);
    return pipeline;
}

bool createBuffer(const GpuContext& ctx, VkDeviceSize size, VkBufferUsageFlags usage, bool hostVisible,
                  GpuBuffer& out) {
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = size;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    if (hostVisible) {
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    }
    VmaAllocationInfo result{};
    if (vmaCreateBuffer(ctx.allocator, &info, &allocInfo, &out.buffer, &out.allocation, &result) != VK_SUCCESS) {
        out = {};
        return false;
    }
    out.mapped = hostVisible ? result.pMappedData : nullptr;
    out.size = size;
    return true;
}

void destroyBuffer(const GpuContext& ctx, GpuBuffer& buffer) {
    if (buffer.buffer != VK_NULL_HANDLE) vmaDestroyBuffer(ctx.allocator, buffer.buffer, buffer.allocation);
    buffer = {};
}

bool createImage(const GpuContext& ctx, VkFormat format, uint32_t width, uint32_t height, uint32_t mipLevels,
                 uint32_t layers, bool cube, VkImageUsageFlags usage, GpuImage& out) {
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.flags = cube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent = {width, height, 1};
    info.mipLevels = mipLevels;
    info.arrayLayers = layers;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    if (vmaCreateImage(ctx.allocator, &info, &allocInfo, &out.image, &out.allocation, nullptr) != VK_SUCCESS) {
        out = {};
        return false;
    }

    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = out.image;
    viewInfo.viewType = cube ? VK_IMAGE_VIEW_TYPE_CUBE : (layers > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D);
    viewInfo.format = format;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels, 0, layers};
    if (vkCreateImageView(ctx.device, &viewInfo, nullptr, &out.view) != VK_SUCCESS) {
        destroyImage(ctx, out);
        return false;
    }
    return true;
}

void destroyImage(const GpuContext& ctx, GpuImage& image) {
    if (image.view != VK_NULL_HANDLE) vkDestroyImageView(ctx.device, image.view, nullptr);
    if (image.image != VK_NULL_HANDLE) vmaDestroyImage(ctx.allocator, image.image, image.allocation);
    image = {};
}

VkImageView createArrayView(const GpuContext& ctx, VkImage image, VkFormat format, uint32_t mip, uint32_t layers) {
    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    viewInfo.format = format;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, mip, 1, 0, layers};
    VkImageView view = VK_NULL_HANDLE;
    if (vkCreateImageView(ctx.device, &viewInfo, nullptr, &view) != VK_SUCCESS) return VK_NULL_HANDLE;
    return view;
}

void imageBarrier(VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout,
                  VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess, VkPipelineStageFlags2 dstStage,
                  VkAccessFlags2 dstAccess, uint32_t baseMip, uint32_t mipCount, uint32_t layers,
                  VkImageAspectFlags aspect) {
    VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    barrier.srcStageMask = srcStage;
    barrier.srcAccessMask = srcAccess;
    barrier.dstStageMask = dstStage;
    barrier.dstAccessMask = dstAccess;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {aspect, baseMip, mipCount, 0, layers};

    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(cmd, &dep);
}

void bufferBarrier(VkCommandBuffer cmd, VkBuffer buffer, VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                   VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess) {
    VkBufferMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
    barrier.srcStageMask = srcStage;
    barrier.srcAccessMask = srcAccess;
    barrier.dstStageMask = dstStage;
    barrier.dstAccessMask = dstAccess;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = buffer;
    barrier.offset = 0;
    barrier.size = VK_WHOLE_SIZE;

    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.bufferMemoryBarrierCount = 1;
    dep.pBufferMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(cmd, &dep);
}

} // namespace engine::core::render
