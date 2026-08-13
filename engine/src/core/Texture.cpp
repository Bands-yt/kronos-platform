#include "core/Texture.hpp"

#include <cstdio>
#include <cstring>
#include "core/Logger.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace engine::core {

namespace {

bool createBuffer(VmaAllocator allocator, VkDeviceSize size, VkBufferUsageFlags usage, VmaAllocationCreateFlags flags,
                   VkBuffer& outBuffer, VmaAllocation& outAllocation) {
    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = flags;

    return vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &outBuffer, &outAllocation, nullptr) == VK_SUCCESS;
}

void transitionImageLayout(VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout,
                            VkAccessFlags2 srcAccess, VkAccessFlags2 dstAccess, VkPipelineStageFlags2 srcStage,
                            VkPipelineStageFlags2 dstStage) {
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
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(cmd, &dep);
}

} // namespace

Texture Texture::uploadPixels(const uint8_t* rgba, int width, int height, bool srgb, VmaAllocator allocator,
                               VkDevice device, VkCommandPool cmdPool, VkQueue queue) {
    Texture result;
    VkDeviceSize imageBytes = static_cast<VkDeviceSize>(width) * static_cast<VkDeviceSize>(height) * 4;

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VmaAllocation stagingAllocation = nullptr;
    if (!createBuffer(allocator, imageBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                       VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
                       stagingBuffer, stagingAllocation)) {
        logError("Texture", "staging buffer creation failed.");
        return result;
    }
    VmaAllocationInfo stagingInfo{};
    vmaGetAllocationInfo(allocator, stagingAllocation, &stagingInfo);
    std::memcpy(stagingInfo.pMappedData, rgba, static_cast<size_t>(imageBytes));

    VkFormat format = srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;

    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo imageAllocInfo{};
    imageAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    if (vmaCreateImage(allocator, &imageInfo, &imageAllocInfo, &result.image_, &result.allocation_, nullptr) !=
        VK_SUCCESS) {
        logError("Texture", "vmaCreateImage failed.");
        vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
        result = Texture{};
        return result;
    }

    VkCommandBufferAllocateInfo cmdAllocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cmdAllocInfo.commandPool = cmdPool;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    // Kronos (Phase 1 stability audit fix): real result check -- see
    // core::uploadToDeviceLocalBuffer() in Mesh.cpp for the identical
    // real bug this mirrors. The image above already real-succeeded, so
    // it (not just the staging buffer) needs real cleanup here too.
    if (vkAllocateCommandBuffers(device, &cmdAllocInfo, &cmd) != VK_SUCCESS) {
        logError("Texture", "vkAllocateCommandBuffers failed.");
        vmaDestroyImage(allocator, result.image_, result.allocation_);
        vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
        result = Texture{};
        return result;
    }

    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    transitionImageLayout(cmd, result.image_, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           VK_ACCESS_2_NONE, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
                           VK_PIPELINE_STAGE_2_TRANSFER_BIT);

    VkBufferImageCopy copyRegion{};
    copyRegion.bufferOffset = 0;
    copyRegion.bufferRowLength = 0;
    copyRegion.bufferImageHeight = 0;
    copyRegion.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copyRegion.imageOffset = {0, 0, 0};
    copyRegion.imageExtent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
    vkCmdCopyBufferToImage(cmd, stagingBuffer, result.image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

    transitionImageLayout(cmd, result.image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                           VK_ACCESS_2_SHADER_READ_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                           VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue); // one-shot, synchronous -- same tradeoff Mesh::uploadFromHost makes, see its comment

    vkFreeCommandBuffers(device, cmdPool, 1, &cmd);
    vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);

    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = result.image_;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(device, &viewInfo, nullptr, &result.view_) != VK_SUCCESS) {
        logError("Texture", "vkCreateImageView failed.");
        vmaDestroyImage(allocator, result.image_, result.allocation_);
        result = Texture{};
        return result;
    }

    result.width_ = width;
    result.height_ = height;
    return result;
}

Texture Texture::loadFromFile(const std::string& path, VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool,
                               VkQueue queue, bool srgb) {
    int width = 0;
    int height = 0;
    int sourceChannels = 0;
    stbi_uc* pixels = stbi_load(path.c_str(), &width, &height, &sourceChannels, STBI_rgb_alpha);
    if (pixels == nullptr) {
        logError("Texture", "failed to load \"%s\": %s", path.c_str(), stbi_failure_reason());
        return Texture{};
    }

    Texture result = uploadPixels(pixels, width, height, srgb, allocator, device, cmdPool, queue);
    stbi_image_free(pixels);

    if (result.isValid()) {
        logInfo("Texture", "loaded \"%s\" (%dx%d, %s)", path.c_str(), width, height, srgb ? "sRGB" : "linear");
    }
    return result;
}

Texture Texture::createSolidColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a, VmaAllocator allocator, VkDevice device,
                                   VkCommandPool cmdPool, VkQueue queue) {
    uint8_t pixel[4] = {r, g, b, a};
    // Solid-color defaults (white albedo, mid-gray metallic/roughness)
    // are data, not authored color art -- always linear (srgb=false), or
    // e.g. a 0.5 gray "no perturbation" fallback would come back subtly
    // wrong after an sRGB->linear decode that was never meant to apply.
    return uploadPixels(pixel, 1, 1, false, allocator, device, cmdPool, queue);
}

// Sprint 16 ("Cinematic Graphics"): the same real upload path
// loadFromFile()/createSolidColor() already use, exposed directly for
// procedurally-*generated* pixel data (core::ProceduralMaterials) rather
// than pixels decoded from a file on disk -- no real texture assets exist
// anywhere in this repo and none can be downloaded (see
// ProceduralMaterials.hpp's own header comment), so this is the real,
// direct path from "generated RGBA bytes" to "a real, sampled GPU
// texture" without a pointless write-to-TGA-then-read-it-back round trip.
Texture Texture::createFromPixels(const uint8_t* rgba, int width, int height, bool srgb, VmaAllocator allocator,
                                   VkDevice device, VkCommandPool cmdPool, VkQueue queue) {
    return uploadPixels(rgba, width, height, srgb, allocator, device, cmdPool, queue);
}

void Texture::destroy(VmaAllocator allocator, VkDevice device) {
    if (view_ != VK_NULL_HANDLE) {
        vkDestroyImageView(device, view_, nullptr);
        view_ = VK_NULL_HANDLE;
    }
    if (image_ != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, image_, allocation_);
        image_ = VK_NULL_HANDLE;
    }
    width_ = 0;
    height_ = 0;
}

uint32_t TextureLibrary::registerTexture(Texture texture) {
    textures_.push_back(std::move(texture));
    return static_cast<uint32_t>(textures_.size() - 1);
}

const Texture* TextureLibrary::get(uint32_t handle) const {
    if (handle >= textures_.size()) return nullptr;
    return &textures_[handle];
}

void TextureLibrary::destroyAll(VmaAllocator allocator, VkDevice device) {
    for (Texture& texture : textures_) {
        texture.destroy(allocator, device);
    }
    textures_.clear();
}

} // namespace engine::core
