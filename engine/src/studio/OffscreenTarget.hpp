#pragma once

#include <volk.h>
#include <vk_mem_alloc.h>

namespace engine::studio {

// Studio's offscreen render target for the Viewport panel
// (docs/ARCHITECTURE.md §4.2/§5): the real scene, rendered via
// core::Renderer::drawSceneInto() into this image, then displayed inside
// the docked Viewport panel via ImGui::Image() -- never the swapchain
// directly (Studio's swapchain pass stays a plain clear behind the docked
// UI; see core::Renderer::setScene()'s doc comment for why).
//
// There is deliberately one frame of latency between "the panel resized
// to X this frame" and "the texture displayed is X": ensureSize() is
// called from Renderer's pre-pass hook (before the swapchain command
// buffer even begins), using the *previous* frame's requested size, while
// ImGui::Image() in the panel itself displays whatever this target held
// at the start of *this* frame. That's the same latency any render-to-
// texture editor viewport has, and is invisible at interactive framerates
// -- not worth adding a second render pass or a stall to eliminate.
class OffscreenTarget {
public:
    ~OffscreenTarget();

    // (Re)creates the color+depth images, sampler, and ImGui texture
    // descriptor if `extent` differs from the current size (or nothing
    // exists yet). No-op otherwise. `colorFormat` must match whatever
    // core::Renderer's scene pipeline was built for (its swapchainFormat())
    // -- dynamic-rendering pipelines are format-specific, and this target
    // is drawn into with that same pipeline.
    //
    // A resize does NOT destroy the previous images/descriptor set
    // in-place -- it retires them and frees them on the *following* call
    // instead. See the .cpp for the same-frame use-after-free this avoids
    // (StudioApp::run() records an ImGui::Image() draw command referencing
    // the pre-resize descriptor set before this runs; that draw data is
    // submitted later in the same renderFrame() call).
    void ensureSize(VmaAllocator allocator, VkDevice device, VkFormat colorFormat, VkFormat depthFormat,
                     VkExtent2D extent);
    void destroy(VmaAllocator allocator, VkDevice device);
    // Destroys the persistent sampler. Separate from destroy() because
    // sampler_ deliberately survives resizes (see its declaration below) --
    // call this once, at real shutdown, after the last destroy().
    void destroySampler(VkDevice device);

    [[nodiscard]] bool isValid() const { return colorImage_ != VK_NULL_HANDLE; }
    [[nodiscard]] VkImage colorImage() const { return colorImage_; }
    [[nodiscard]] VkImageView colorView() const { return colorImageView_; }
    [[nodiscard]] VkImage depthImage() const { return depthImage_; }
    [[nodiscard]] VkImageView depthView() const { return depthImageView_; }
    [[nodiscard]] VkExtent2D extent() const { return extent_; }

    // The ImTextureID (via ImGui_ImplVulkan_AddTexture) for ImGui::Image()
    // -- valid once isValid() is true, stable across frames, only changes
    // when ensureSize() actually resizes (recreating the underlying view).
    [[nodiscard]] VkDescriptorSet imguiTextureId() const { return imguiDescriptorSet_; }

private:
    // Frees whatever ensureSize() retired on its *previous* call -- see
    // ensureSize()'s doc comment. Safe to call with nothing retired
    // (no-op).
    void destroyRetired(VmaAllocator allocator, VkDevice device);

    VkExtent2D extent_{0, 0};

    VkImage colorImage_ = VK_NULL_HANDLE;
    VmaAllocation colorAllocation_ = nullptr;
    VkImageView colorImageView_ = VK_NULL_HANDLE;

    VkImage depthImage_ = VK_NULL_HANDLE;
    VmaAllocation depthAllocation_ = nullptr;
    VkImageView depthImageView_ = VK_NULL_HANDLE;

    VkSampler sampler_ = VK_NULL_HANDLE; // persistent across resizes, created once
    VkDescriptorSet imguiDescriptorSet_ = VK_NULL_HANDLE;

    // One-call-delayed destruction of whatever the previous resize
    // replaced -- see ensureSize()'s doc comment.
    VkImage retiredColorImage_ = VK_NULL_HANDLE;
    VmaAllocation retiredColorAllocation_ = nullptr;
    VkImageView retiredColorImageView_ = VK_NULL_HANDLE;
    VkImage retiredDepthImage_ = VK_NULL_HANDLE;
    VmaAllocation retiredDepthAllocation_ = nullptr;
    VkImageView retiredDepthImageView_ = VK_NULL_HANDLE;
    VkDescriptorSet retiredDescriptorSet_ = VK_NULL_HANDLE;
};

} // namespace engine::studio
