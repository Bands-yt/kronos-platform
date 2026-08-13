#include "trailer/CaptureRig.hpp"

#include <cstdio>

#include "publishing/ThumbnailCapture.hpp"

namespace engine::trailer {

std::string frameFilename(int frameIndex) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "frame_%06d.ppm", frameIndex);
    return buffer;
}

CaptureRig::~CaptureRig() {
    // Real cleanup happens in shutdown() -- same real "destructor does
    // nothing, caller explicitly tears down before allocator/device are
    // gone" contract studio::OffscreenTarget's own destructor already
    // documents.
}

bool CaptureRig::initialize(core::Renderer& renderer, VkExtent2D extent) {
    if (extent.width == 0 || extent.height == 0) {
        std::fprintf(stderr, "CaptureRig::initialize: zero extent.\n");
        return false;
    }

    auxiliaryScene_ = renderer.createAuxiliaryScene();
    if (auxiliaryScene_ == core::Renderer::kInvalidAuxiliaryScene) return false; // logged by createAuxiliaryScene() itself

    extent_ = extent;
    colorFormat_ = renderer.swapchainFormat();

    VkImageCreateInfo colorInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    colorInfo.imageType = VK_IMAGE_TYPE_2D;
    colorInfo.extent = {extent.width, extent.height, 1};
    colorInfo.mipLevels = 1;
    colorInfo.arrayLayers = 1;
    colorInfo.format = colorFormat_;
    colorInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    colorInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    colorInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    colorInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    if (vmaCreateImage(renderer.allocator(), &colorInfo, &allocInfo, &colorImage_, &colorAllocation_, nullptr) != VK_SUCCESS) {
        std::fprintf(stderr, "CaptureRig::initialize: vmaCreateImage (color) failed.\n");
        return false;
    }

    VkImageCreateInfo depthInfo = colorInfo;
    depthInfo.format = renderer.depthFormat();
    // SAMPLED_BIT (Sprint 16): this rig's drawSceneInto() call now runs
    // Renderer's cinematic post-FX pass (SSAO/DOF/motion blur) whenever
    // Cinematic Mode is on, which reads depth back as a real texture --
    // see Renderer::createDepthResources()'s matching comment for the
    // main swapchain depth buffer and studio::OffscreenTarget::ensureSize()'s
    // for Studio's own aux-scene depth buffers (the two other real depth-
    // buffer creation sites this same real fix already reached; this one
    // was missed in that pass -- live-verified via engine_runtime
    // --miningsim: without this flag, sampling this then-not-SAMPLED
    // image in shaders/cinematic.frag is undefined behavior -- silently
    // garbage depth values, not a validation error, since no validation
    // layer is installed in this environment -- which reconstructed
    // wildly wrong world positions and pegged the real DOF circle-of-
    // confusion near its maximum almost everywhere, reading as a
    // uniform, scene-wide blur rather than real, correctly-falling-off
    // depth-of-field).
    depthInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    if (vmaCreateImage(renderer.allocator(), &depthInfo, &allocInfo, &depthImage_, &depthAllocation_, nullptr) != VK_SUCCESS) {
        std::fprintf(stderr, "CaptureRig::initialize: vmaCreateImage (depth) failed.\n");
        return false;
    }

    VkImageViewCreateInfo colorViewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    colorViewInfo.image = colorImage_;
    colorViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    colorViewInfo.format = colorFormat_;
    colorViewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(renderer.device(), &colorViewInfo, nullptr, &colorImageView_) != VK_SUCCESS) {
        std::fprintf(stderr, "CaptureRig::initialize: vkCreateImageView (color) failed.\n");
        return false;
    }

    VkImageViewCreateInfo depthViewInfo = colorViewInfo;
    depthViewInfo.image = depthImage_;
    depthViewInfo.format = renderer.depthFormat();
    depthViewInfo.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(renderer.device(), &depthViewInfo, nullptr, &depthImageView_) != VK_SUCCESS) {
        std::fprintf(stderr, "CaptureRig::initialize: vkCreateImageView (depth) failed.\n");
        return false;
    }

    return true;
}

bool CaptureRig::captureFrame(core::Renderer& renderer, core::ECS& ecs, core::MeshLibrary& meshLibrary,
                               core::TextureLibrary& textureLibrary, const core::Camera& camera,
                               const std::string& outputDirectory, int frameIndex) {
    if (!isValid()) {
        std::fprintf(stderr, "CaptureRig::captureFrame: not initialized.\n");
        return false;
    }

    // Real, self-contained one-shot render: its own command buffer,
    // submitted and waited on before captureThumbnailToFile()'s own
    // separate real one-shot readback runs below -- that function
    // manages its own internal command buffer/submission and has no
    // parameter to accept an external one, so two real, sequential GPU
    // round trips per captured frame (render, then readback) is the
    // correct, low-risk choice here: reusing that already-real, already-
    // tested Sprint 13 function unchanged, not risking a signature change
    // that could regress Studio's own publishing flow.
    VkCommandBufferAllocateInfo cmdAllocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cmdAllocInfo.commandPool = renderer.commandPool();
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(renderer.device(), &cmdAllocInfo, &cmd) != VK_SUCCESS) {
        std::fprintf(stderr, "CaptureRig::captureFrame: vkAllocateCommandBuffers failed.\n");
        return false;
    }

    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    renderer.drawSceneInto(auxiliaryScene_, cmd, colorImage_, colorImageView_, depthImage_, depthImageView_, extent_,
                            camera, ecs, meshLibrary, captureParticleSystem_, textureLibrary);
    // drawSceneInto() leaves colorImage_ in COLOR_ATTACHMENT_OPTIMAL (see
    // that method's own doc comment) -- transition to
    // TRANSFER_SRC_OPTIMAL directly (skipping the SHADER_READ_ONLY_OPTIMAL
    // intermediate ThumbnailCameraRig uses, since this image is never
    // sampled by anything, only ever read back) so
    // captureThumbnailToFile()'s own internal transition
    // (SHADER_READ_ONLY_OPTIMAL -> TRANSFER_SRC_OPTIMAL) has a real,
    // valid starting layout to transition *from*.
    renderer.transitionImage(cmd, colorImage_, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                              VK_ACCESS_2_SHADER_READ_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                              VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    vkQueueSubmit(renderer.graphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(renderer.graphicsQueue());
    vkFreeCommandBuffers(renderer.device(), renderer.commandPool(), 1, &cmd);

    std::string outputPath = outputDirectory + "/" + frameFilename(frameIndex);
    return publishing::captureThumbnailToFile(renderer, colorImage_, colorFormat_, extent_, outputPath);
}

void CaptureRig::shutdown(core::Renderer& renderer) {
    if (colorImageView_ != VK_NULL_HANDLE) {
        vkDestroyImageView(renderer.device(), colorImageView_, nullptr);
        colorImageView_ = VK_NULL_HANDLE;
    }
    if (depthImageView_ != VK_NULL_HANDLE) {
        vkDestroyImageView(renderer.device(), depthImageView_, nullptr);
        depthImageView_ = VK_NULL_HANDLE;
    }
    if (colorImage_ != VK_NULL_HANDLE) {
        vmaDestroyImage(renderer.allocator(), colorImage_, colorAllocation_);
        colorImage_ = VK_NULL_HANDLE;
    }
    if (depthImage_ != VK_NULL_HANDLE) {
        vmaDestroyImage(renderer.allocator(), depthImage_, depthAllocation_);
        depthImage_ = VK_NULL_HANDLE;
    }
    if (auxiliaryScene_ != core::Renderer::kInvalidAuxiliaryScene) {
        renderer.destroyAuxiliaryScene(auxiliaryScene_);
        auxiliaryScene_ = core::Renderer::kInvalidAuxiliaryScene;
    }
}

} // namespace engine::trailer
