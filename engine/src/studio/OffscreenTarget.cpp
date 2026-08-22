#include "studio/OffscreenTarget.hpp"

#include <cstdio>
#include "core/Logger.hpp"

#include <backends/imgui_impl_vulkan.h>

namespace engine::studio {

namespace {
bool sameExtent(VkExtent2D a, VkExtent2D b) { return a.width == b.width && a.height == b.height; }
} // namespace

OffscreenTarget::~OffscreenTarget() {
    // Real cleanup happens in destroy() -- StudioApp calls it explicitly
    // with the allocator/device before those are torn down, same
    // ordering contract as core::Application's MeshLibrary (see
    // Renderer::shutdown()'s NOTE). This destructor intentionally does
    // nothing: by the time it runs, allocator/device may already be gone.
}

void OffscreenTarget::ensureSize(VmaAllocator allocator, VkDevice device, VkFormat colorFormat, VkFormat depthFormat,
                                  VkExtent2D extent) {
    // Whatever was retired by the *previous* call to ensureSize() is safe
    // to actually free now -- see the retire step below for why it
    // couldn't be freed immediately when it was retired. This runs
    // unconditionally (even on frames that don't resize) so retired
    // resources don't sit around leaking VRAM until the next resize
    // happens to occur.
    destroyRetired(allocator, device);

    if (extent.width == 0 || extent.height == 0) return; // panel not visible/collapsed this frame
    if (isValid() && sameExtent(extent, extent_)) return;

    if (isValid()) {
        // Retire the current resources instead of destroying them here.
        //
        // Real bug this fixes (reproduced via coredumpctl -- SIGSEGV deep
        // in ImGui_ImplVulkan_RenderDrawData's vkCmdBindDescriptorSets):
        // StudioApp::run() calls viewportPanel_.draw() -- which reads
        // imguiTextureId()/extent() as of *before* this call and records
        // an ImGui::Image() draw command referencing them -- *before*
        // renderer_.renderFrame() runs. renderFrame() then runs this
        // pre-pass callback first (where ensureSize() resizes for the
        // size just measured) and the overlay callback *later in that
        // same call* (where the ImDrawData built moments ago, still
        // referencing the pre-resize descriptor set, actually gets
        // submitted). Destroying that descriptor set/images here, as the
        // old code did, frees them before they're ever bound -- a
        // same-frame use-after-free, not a GPU-in-flight race, which is
        // why a plain vkDeviceWaitIdle() alone did not fix it (nothing
        // had been submitted to the GPU yet for it to wait on).
        //
        // Deferring the actual free to the *next* ensureSize() call (see
        // destroyRetired() above) guarantees a full renderFrame() cycle
        // -- pre-pass and overlay both -- has completed for the frame
        // that referenced these resources before they're freed.
        retiredColorImage_ = colorImage_;
        retiredColorAllocation_ = colorAllocation_;
        retiredColorImageView_ = colorImageView_;
        retiredDepthImage_ = depthImage_;
        retiredDepthAllocation_ = depthAllocation_;
        retiredDepthImageView_ = depthImageView_;
        retiredDescriptorSet_ = imguiDescriptorSet_;
        colorImage_ = VK_NULL_HANDLE;
        colorAllocation_ = nullptr;
        colorImageView_ = VK_NULL_HANDLE;
        depthImage_ = VK_NULL_HANDLE;
        depthAllocation_ = nullptr;
        depthImageView_ = VK_NULL_HANDLE;
        imguiDescriptorSet_ = VK_NULL_HANDLE;
    }

    extent_ = extent;

    VkImageCreateInfo colorInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    colorInfo.imageType = VK_IMAGE_TYPE_2D;
    colorInfo.extent = {extent.width, extent.height, 1};
    colorInfo.mipLevels = 1;
    colorInfo.arrayLayers = 1;
    colorInfo.format = colorFormat;
    colorInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    colorInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    colorInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    colorInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    if (vmaCreateImage(allocator, &colorInfo, &allocInfo, &colorImage_, &colorAllocation_, nullptr) != VK_SUCCESS) {
        core::logError("OffscreenTarget", "vmaCreateImage (color) failed.");
        return;
    }

    VkImageCreateInfo depthInfo = colorInfo;
    depthInfo.format = depthFormat;
    // SAMPLED_BIT (Sprint 16): every drawSceneInto() consumer -- this one
    // included -- can now run Renderer's cinematic post-FX pass (SSAO/DOF/
    // motion blur), which reads depth back as a real texture. See
    // Renderer::createDepthResources()'s matching comment for the main
    // swapchain depth buffer.
    depthInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    if (vmaCreateImage(allocator, &depthInfo, &allocInfo, &depthImage_, &depthAllocation_, nullptr) != VK_SUCCESS) {
        core::logError("OffscreenTarget", "vmaCreateImage (depth) failed.");
        return;
    }

    // Kronos (Phase 1 stability audit fix): real result checks on all
    // three calls below -- previously unchecked, inconsistent with the
    // careful VkResult checking every other resource creation in this
    // function already does. On failure, destroy() tears down whatever
    // of colorImage_/depthImage_/colorImageView_/depthImageView_ already
    // real-succeeded (it null-checks each one) rather than leaving this
    // target in a half-built state.
    VkImageViewCreateInfo colorViewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    colorViewInfo.image = colorImage_;
    colorViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    colorViewInfo.format = colorFormat;
    colorViewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(device, &colorViewInfo, nullptr, &colorImageView_) != VK_SUCCESS) {
        core::logError("OffscreenTarget", "vkCreateImageView (color) failed.");
        destroy(allocator, device);
        return;
    }

    VkImageViewCreateInfo depthViewInfo = colorViewInfo;
    depthViewInfo.image = depthImage_;
    depthViewInfo.format = depthFormat;
    depthViewInfo.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(device, &depthViewInfo, nullptr, &depthImageView_) != VK_SUCCESS) {
        core::logError("OffscreenTarget", "vkCreateImageView (depth) failed.");
        destroy(allocator, device);
        return;
    }

    if (sampler_ == VK_NULL_HANDLE) {
        VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        if (vkCreateSampler(device, &samplerInfo, nullptr, &sampler_) != VK_SUCCESS) {
            core::logError("OffscreenTarget", "vkCreateSampler failed.");
            destroy(allocator, device);
            return;
        }
    }

    imguiDescriptorSet_ = ImGui_ImplVulkan_AddTexture(sampler_, colorImageView_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void OffscreenTarget::destroyRetired(VmaAllocator allocator, VkDevice device) {
    if (retiredDescriptorSet_ == VK_NULL_HANDLE && retiredColorImage_ == VK_NULL_HANDLE &&
        retiredDepthImage_ == VK_NULL_HANDLE) {
        return;
    }

    // A full renderFrame() cycle has elapsed on the *CPU* timeline since
    // these were retired (see ensureSize()'s comment), but that only
    // guarantees the ImDrawData referencing them was submitted, not that
    // the GPU has actually finished executing it -- this app keeps
    // multiple frames in flight (see Renderer's "N frames in flight" init
    // log). Waiting here, rather than tracking per-submission fences, is
    // the right-sized fix for a rare, human-interactive event like a
    // viewport resize -- a full-stall wait is imperceptible at that
    // frequency, unlike it would be per-frame.
    vkDeviceWaitIdle(device);

    if (retiredDescriptorSet_ != VK_NULL_HANDLE) {
        ImGui_ImplVulkan_RemoveTexture(retiredDescriptorSet_);
        retiredDescriptorSet_ = VK_NULL_HANDLE;
    }
    if (retiredColorImageView_ != VK_NULL_HANDLE) {
        vkDestroyImageView(device, retiredColorImageView_, nullptr);
        retiredColorImageView_ = VK_NULL_HANDLE;
    }
    if (retiredDepthImageView_ != VK_NULL_HANDLE) {
        vkDestroyImageView(device, retiredDepthImageView_, nullptr);
        retiredDepthImageView_ = VK_NULL_HANDLE;
    }
    if (retiredColorImage_ != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, retiredColorImage_, retiredColorAllocation_);
        retiredColorImage_ = VK_NULL_HANDLE;
    }
    if (retiredDepthImage_ != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, retiredDepthImage_, retiredDepthAllocation_);
        retiredDepthImage_ = VK_NULL_HANDLE;
    }
}

void OffscreenTarget::destroy(VmaAllocator allocator, VkDevice device) {
    destroyRetired(allocator, device);

    if (imguiDescriptorSet_ != VK_NULL_HANDLE) {
        ImGui_ImplVulkan_RemoveTexture(imguiDescriptorSet_);
        imguiDescriptorSet_ = VK_NULL_HANDLE;
    }
    if (colorImageView_ != VK_NULL_HANDLE) {
        vkDestroyImageView(device, colorImageView_, nullptr);
        colorImageView_ = VK_NULL_HANDLE;
    }
    if (depthImageView_ != VK_NULL_HANDLE) {
        vkDestroyImageView(device, depthImageView_, nullptr);
        depthImageView_ = VK_NULL_HANDLE;
    }
    if (colorImage_ != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, colorImage_, colorAllocation_);
        colorImage_ = VK_NULL_HANDLE;
    }
    if (depthImage_ != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, depthImage_, depthAllocation_);
        depthImage_ = VK_NULL_HANDLE;
    }
    // sampler_ is intentionally NOT destroyed here -- it's persistent
    // across resizes (see header). destroySampler() below is the final
    // teardown, called once by StudioApp after the last destroy().
}

void OffscreenTarget::destroySampler(VkDevice device) {
    if (sampler_ == VK_NULL_HANDLE) return;
    vkDestroySampler(device, sampler_, nullptr);
    sampler_ = VK_NULL_HANDLE;
}

} // namespace engine::studio
