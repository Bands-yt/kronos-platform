#include "publishing/ThumbnailCapture.hpp"

#include <cstdio>
#include <fstream>

#include "core/Logger.hpp"
#include "core/Renderer.hpp"

namespace engine::publishing {

bool captureThumbnailToFile(core::Renderer& renderer, VkImage colorImage, VkFormat colorFormat, VkExtent2D extent,
                             const std::string& outputPath) {
    if (extent.width == 0 || extent.height == 0) {
        core::logError("ThumbnailCapture", "zero-sized extent, nothing to capture.");
        return false;
    }

    VkDevice device = renderer.device();
    VmaAllocator allocator = renderer.allocator();
    VkDeviceSize imageBytes = static_cast<VkDeviceSize>(extent.width) * static_cast<VkDeviceSize>(extent.height) * 4;

    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = imageBytes;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VmaAllocation stagingAllocation = nullptr;
    if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &stagingBuffer, &stagingAllocation, nullptr) != VK_SUCCESS) {
        core::logError("ThumbnailCapture", "staging buffer creation failed.");
        return false;
    }

    VkCommandBufferAllocateInfo cmdAllocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cmdAllocInfo.commandPool = renderer.commandPool();
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    // Kronos (Phase 1 stability audit fix): real result check -- see
    // core::uploadToDeviceLocalBuffer() in Mesh.cpp for the identical
    // real bug this mirrors.
    if (vkAllocateCommandBuffers(device, &cmdAllocInfo, &cmd) != VK_SUCCESS) {
        core::logError("ThumbnailCapture", "vkAllocateCommandBuffers failed.");
        vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
        return false;
    }

    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    renderer.transitionImage(cmd, colorImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                              VK_ACCESS_2_SHADER_READ_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
                              VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT);

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {extent.width, extent.height, 1};
    vkCmdCopyImageToBuffer(cmd, colorImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, stagingBuffer, 1, &region);

    renderer.transitionImage(cmd, colorImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                              VK_ACCESS_2_TRANSFER_READ_BIT, VK_ACCESS_2_SHADER_READ_BIT,
                              VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    vkQueueSubmit(renderer.graphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
    // One-shot, synchronous -- see this function's own header comment
    // for why that's the right tradeoff for an explicit capture action,
    // the same as core::Texture::uploadPixels()'s own vkQueueWaitIdle.
    vkQueueWaitIdle(renderer.graphicsQueue());
    vkFreeCommandBuffers(device, renderer.commandPool(), 1, &cmd);

    VmaAllocationInfo stagingInfo{};
    vmaGetAllocationInfo(allocator, stagingAllocation, &stagingInfo);
    const auto* pixels = static_cast<const uint8_t*>(stagingInfo.pMappedData);

    // The swapchain format (what OffscreenTarget is always created with,
    // see its own header comment) is chosen as B8G8R8A8_* when available
    // (core::Renderer's chooseSurfaceFormat()) -- real memory byte order
    // is [B, G, R, A] per pixel for that format family, so a real RGB
    // PPM write needs the R/B channels swapped; any other format is
    // assumed already R-first.
    bool swapRedBlue = colorFormat == VK_FORMAT_B8G8R8A8_SRGB || colorFormat == VK_FORMAT_B8G8R8A8_UNORM;

    bool ok = true;
    {
        std::ofstream file(outputPath, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) {
            core::logError("ThumbnailCapture", "could not open \"%s\" for writing.", outputPath.c_str());
            ok = false;
        } else {
            file << "P6\n" << extent.width << " " << extent.height << "\n255\n";
            for (uint32_t i = 0; i < extent.width * extent.height; ++i) {
                const uint8_t* texel = pixels + static_cast<size_t>(i) * 4;
                uint8_t rgb[3] = {texel[swapRedBlue ? 2 : 0], texel[1], texel[swapRedBlue ? 0 : 2]};
                file.write(reinterpret_cast<const char*>(rgb), 3);
            }
            ok = file.good();
        }
    }

    vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
    return ok;
}

} // namespace engine::publishing
