#include "studio/SecondaryViewport.hpp"

#include <algorithm>
#include <cstdio>

#include <SDL2/SDL_vulkan.h>

#include "core/SwapchainSelection.hpp"

namespace engine::studio {

SecondaryViewport::~SecondaryViewport() {
    // Real cleanup happens in shutdown() -- same "destructor does nothing,
    // caller explicitly tears down before allocator/device are gone"
    // contract trailer::CaptureRig/studio::OffscreenTarget already use.
}

bool SecondaryViewport::initialize(core::Renderer& renderer, const core::Window::CreateInfo& windowInfo) {
    if (!window_.initialize(windowInfo)) {
        std::fprintf(stderr, "SecondaryViewport::initialize: %s\n", window_.lastError().c_str());
        return false;
    }
    window_.show(); // Kronos ("flickering when opening a game") only applies to the very first frame of the *process*; a tool window opened mid-session has nothing stale to hide behind

    auxiliaryScene_ = renderer.createAuxiliaryScene();
    if (auxiliaryScene_ == core::Renderer::kInvalidAuxiliaryScene) {
        window_.shutdown();
        return false; // logged by createAuxiliaryScene() itself
    }

    if (!createSurfaceAndSwapchain(renderer)) {
        renderer.destroyAuxiliaryScene(auxiliaryScene_);
        auxiliaryScene_ = core::Renderer::kInvalidAuxiliaryScene;
        window_.shutdown();
        return false;
    }

    return true;
}

bool SecondaryViewport::createSurfaceAndSwapchain(core::Renderer& renderer) {
    surface_ = window_.createSurface(renderer.instance());
    if (surface_ == VK_NULL_HANDLE) {
        std::fprintf(stderr, "SecondaryViewport::initialize: createSurface failed.\n");
        return false;
    }

    VkBool32 presentSupported = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(renderer.physicalDevice(), renderer.graphicsQueueFamily(), surface_,
                                          &presentSupported);
    if (presentSupported != VK_TRUE) {
        // Real, honest failure -- see this class's own header comment:
        // every desktop platform this engine targets supports presenting
        // from the same queue family to a second window on the same
        // physical device, but this is the correct Vulkan-spec gate to
        // check rather than assume.
        std::fprintf(stderr,
                      "SecondaryViewport::initialize: graphics queue family does not support presenting to this "
                      "window's surface.\n");
        vkDestroySurfaceKHR(renderer.instance(), surface_, nullptr);
        surface_ = VK_NULL_HANDLE;
        return false;
    }

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(renderer.physicalDevice(), surface_, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(renderer.physicalDevice(), surface_, &formatCount, formats.data());

    // Real, required match, not an independent choice -- see this
    // class's own header comment on why this window's format must equal
    // the main window's.
    const VkFormat requiredFormat = renderer.swapchainFormat();
    const bool formatSupported =
        std::any_of(formats.begin(), formats.end(), [&](const VkSurfaceFormatKHR& f) { return f.format == requiredFormat; });
    if (!formatSupported) {
        std::fprintf(stderr,
                      "SecondaryViewport::initialize: this window's surface does not support the main window's "
                      "swapchain format -- cannot share drawSceneInto()'s pipelines.\n");
        vkDestroySurfaceKHR(renderer.instance(), surface_, nullptr);
        surface_ = VK_NULL_HANDLE;
        return false;
    }
    VkColorSpaceKHR colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    for (const auto& f : formats) {
        if (f.format == requiredFormat) {
            colorSpace = f.colorSpace;
            break;
        }
    }

    uint32_t presentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(renderer.physicalDevice(), surface_, &presentModeCount, nullptr);
    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(renderer.physicalDevice(), surface_, &presentModeCount, presentModes.data());

    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(renderer.physicalDevice(), surface_, &caps);

    // Kronos ("UI/UX Revamp" -- vsync default): always FIFO for this
    // window -- a background editor tool has no legitimate need for
    // MAILBOX's uncapped-frame-rate tradeoff (see core::choosePresentMode's
    // own comment on the real fan-spike/coil-whine finding behind that
    // default), and this window's own idle-most-of-the-time usage
    // pattern is exactly the case that finding was about.
    VkPresentModeKHR presentMode = core::choosePresentMode(presentModes, /*vsyncEnabled=*/true);
    extent_ = core::chooseExtent(caps, window_.width(), window_.height());
    if (extent_.width == 0 || extent_.height == 0) return true; // minimized at creation -- real, honest no-op, renderFrame() re-checks every call

    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount) imageCount = caps.maxImageCount;

    VkSwapchainCreateInfoKHR createInfo{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    createInfo.surface = surface_;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = requiredFormat;
    createInfo.imageColorSpace = colorSpace;
    createInfo.imageExtent = extent_;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    createInfo.preTransform = caps.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    if (vkCreateSwapchainKHR(renderer.device(), &createInfo, nullptr, &swapchain_) != VK_SUCCESS) {
        std::fprintf(stderr, "SecondaryViewport::initialize: vkCreateSwapchainKHR failed.\n");
        return false;
    }
    colorFormat_ = requiredFormat;

    uint32_t actualCount = 0;
    vkGetSwapchainImagesKHR(renderer.device(), swapchain_, &actualCount, nullptr);
    images_.resize(actualCount);
    vkGetSwapchainImagesKHR(renderer.device(), swapchain_, &actualCount, images_.data());

    imageViews_.resize(actualCount);
    for (size_t i = 0; i < actualCount; ++i) {
        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image = images_[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = colorFormat_;
        viewInfo.components = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                                VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
        viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        if (vkCreateImageView(renderer.device(), &viewInfo, nullptr, &imageViews_[i]) != VK_SUCCESS) {
            std::fprintf(stderr, "SecondaryViewport::initialize: vkCreateImageView failed for image %zu.\n", i);
            return false;
        }
    }

    VkImageCreateInfo depthInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    depthInfo.imageType = VK_IMAGE_TYPE_2D;
    depthInfo.extent = {extent_.width, extent_.height, 1};
    depthInfo.mipLevels = 1;
    depthInfo.arrayLayers = 1;
    depthInfo.format = renderer.depthFormat();
    depthInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    depthInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    // SAMPLED_BIT: same real reason as trailer::CaptureRig::initialize()'s
    // own depth image -- Renderer's cinematic post-FX pass (SSAO/DOF/
    // motion blur) reads depth back as a real texture whenever Cinematic
    // Mode is on; omitting this is the exact silent-garbage-depth bug
    // that function's own header comment documents.
    depthInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    depthInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    depthInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    if (vmaCreateImage(renderer.allocator(), &depthInfo, &allocInfo, &depthImage_, &depthAllocation_, nullptr) !=
        VK_SUCCESS) {
        std::fprintf(stderr, "SecondaryViewport::initialize: vmaCreateImage (depth) failed.\n");
        return false;
    }

    VkImageViewCreateInfo depthViewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    depthViewInfo.image = depthImage_;
    depthViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    depthViewInfo.format = renderer.depthFormat();
    depthViewInfo.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(renderer.device(), &depthViewInfo, nullptr, &depthImageView_) != VK_SUCCESS) {
        std::fprintf(stderr, "SecondaryViewport::initialize: vkCreateImageView (depth) failed.\n");
        return false;
    }

    VkSemaphoreCreateInfo semInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    if (vkCreateSemaphore(renderer.device(), &semInfo, nullptr, &imageAvailable_) != VK_SUCCESS) {
        std::fprintf(stderr, "SecondaryViewport::initialize: vkCreateSemaphore failed.\n");
        return false;
    }

    return true;
}

void SecondaryViewport::destroySwapchainResources(core::Renderer& renderer) {
    // vkQueueWaitIdle() before tearing anything down -- renderFrame()
    // never leaves GPU work in flight past its own return (see that
    // method's own header comment), but this guards the same invariant
    // explicitly rather than relying on caller discipline.
    if (renderer.device() != VK_NULL_HANDLE) vkQueueWaitIdle(renderer.graphicsQueue());

    if (imageAvailable_ != VK_NULL_HANDLE) vkDestroySemaphore(renderer.device(), imageAvailable_, nullptr);
    imageAvailable_ = VK_NULL_HANDLE;

    if (depthImageView_ != VK_NULL_HANDLE) vkDestroyImageView(renderer.device(), depthImageView_, nullptr);
    depthImageView_ = VK_NULL_HANDLE;
    if (depthImage_ != VK_NULL_HANDLE) vmaDestroyImage(renderer.allocator(), depthImage_, depthAllocation_);
    depthImage_ = VK_NULL_HANDLE;
    depthAllocation_ = VK_NULL_HANDLE;

    for (VkImageView view : imageViews_) {
        if (view != VK_NULL_HANDLE) vkDestroyImageView(renderer.device(), view, nullptr);
    }
    imageViews_.clear();
    images_.clear();

    if (swapchain_ != VK_NULL_HANDLE) vkDestroySwapchainKHR(renderer.device(), swapchain_, nullptr);
    swapchain_ = VK_NULL_HANDLE;

    if (surface_ != VK_NULL_HANDLE) vkDestroySurfaceKHR(renderer.instance(), surface_, nullptr);
    surface_ = VK_NULL_HANDLE;
}

void SecondaryViewport::shutdown(core::Renderer& renderer) {
    destroySwapchainResources(renderer);
    if (auxiliaryScene_ != core::Renderer::kInvalidAuxiliaryScene) {
        renderer.destroyAuxiliaryScene(auxiliaryScene_);
        auxiliaryScene_ = core::Renderer::kInvalidAuxiliaryScene;
    }
    window_.shutdown();
    closeRequested_ = false;
    resizePending_ = false;
}

bool SecondaryViewport::recreateSwapchain(core::Renderer& renderer) {
    destroySwapchainResources(renderer);
    resizePending_ = false;
    return createSurfaceAndSwapchain(renderer);
}

void SecondaryViewport::handleEvent(const SDL_Event& event) {
    if (event.type != SDL_WINDOWEVENT) return;
    if (event.window.windowID != windowId()) return; // real windowId() filter -- see this class's own header comment

    switch (event.window.event) {
        case SDL_WINDOWEVENT_CLOSE:
            closeRequested_ = true;
            break;
        case SDL_WINDOWEVENT_RESIZED:
        case SDL_WINDOWEVENT_SIZE_CHANGED:
            resizePending_ = true;
            break;
        default:
            break;
    }
}

void SecondaryViewport::renderFrame(core::Renderer& renderer, core::ECS& ecs, core::MeshLibrary& meshLibrary,
                                     core::TextureLibrary& textureLibrary, const core::Camera& camera) {
    if (resizePending_ || swapchain_ == VK_NULL_HANDLE) {
        if (!recreateSwapchain(renderer)) return; // logged by createSurfaceAndSwapchain() itself
    }
    if (extent_.width == 0 || extent_.height == 0 || swapchain_ == VK_NULL_HANDLE) return; // still minimized

    uint32_t imageIndex = 0;
    VkResult acquireResult =
        vkAcquireNextImageKHR(renderer.device(), swapchain_, UINT64_MAX, imageAvailable_, VK_NULL_HANDLE, &imageIndex);
    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
        resizePending_ = true;
        return;
    }
    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
        std::fprintf(stderr, "SecondaryViewport::renderFrame: vkAcquireNextImageKHR failed (VkResult=%d).\n",
                     static_cast<int>(acquireResult));
        return;
    }

    VkCommandBufferAllocateInfo cmdAllocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cmdAllocInfo.commandPool = renderer.commandPool();
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(renderer.device(), &cmdAllocInfo, &cmd) != VK_SUCCESS) {
        std::fprintf(stderr, "SecondaryViewport::renderFrame: vkAllocateCommandBuffers failed.\n");
        return;
    }

    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    renderer.drawSceneInto(auxiliaryScene_, cmd, images_[imageIndex], imageViews_[imageIndex], depthImage_,
                            depthImageView_, extent_, camera, ecs, meshLibrary, particleSystem_, textureLibrary);
    renderer.transitionImage(cmd, images_[imageIndex], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                              VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_2_NONE,
                              VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT);

    vkEndCommandBuffer(cmd);

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &imageAvailable_;
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    vkQueueSubmit(renderer.graphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
    // Real, honest simplicity -- see this class's own header comment on
    // why a full wait-idle (not a fence/semaphore present-wait ring) is
    // the accepted tradeoff here: it also guarantees this call's own
    // vkFreeCommandBuffers below never races the GPU still reading `cmd`.
    vkQueueWaitIdle(renderer.graphicsQueue());
    vkFreeCommandBuffers(renderer.device(), renderer.commandPool(), 1, &cmd);

    VkPresentInfoKHR presentInfo{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain_;
    presentInfo.pImageIndices = &imageIndex;
    VkResult presentResult = vkQueuePresentKHR(renderer.graphicsQueue(), &presentInfo);
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) {
        resizePending_ = true;
    } else if (presentResult != VK_SUCCESS) {
        std::fprintf(stderr, "SecondaryViewport::renderFrame: vkQueuePresentKHR failed (VkResult=%d).\n",
                     static_cast<int>(presentResult));
    }
}

} // namespace engine::studio
