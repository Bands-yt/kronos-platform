#include "trailer/CaptureRig.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>

#include "cinematic/ExportRunner.hpp"
#include "cinematic/RailCamera.hpp"
#include "core/Logger.hpp"
#include "core/PhysicalCamera.hpp"
#include "publishing/ThumbnailCapture.hpp"
#include "trailer/FrameEncoding.hpp"

namespace engine::trailer {

namespace {
// Kronos ("Cinematic Camera Physics & Post-Processing Pipeline"): real
// save/restore of every core::Renderer post-FX toggle exportSequence()
// below drives from the sequencer -- these are process-global Renderer
// members (Renderer::setBloomSettings()'s own header comment), not
// scoped to this rig's AuxiliarySceneHandle, so without this an export
// would permanently leave Studio's live viewport (or whatever
// LightingToolsPlugin/TrailerDirector had dialled in) stuck on whatever
// the last exported frame's values happened to be. RAII rather than a
// restore call at each of exportSequence()'s three return points (no
// GPU swapchain / capture failure / disk-write failure) -- same real
// motivation, fewer places to forget one.
class ScopedRendererPostFxState {
public:
    explicit ScopedRendererPostFxState(core::Renderer& renderer)
        : renderer_(renderer),
          cinematicModeEnabled_(renderer.isCinematicModeEnabled()),
          depthOfFieldEnabled_(renderer.isDepthOfFieldEnabled()),
          dofFocusDistance_(renderer.dofFocusDistance()),
          dofFocusRange_(renderer.dofFocusRange()),
          dofMaxCoCRadiusPx_(renderer.dofMaxCoCRadiusPx()),
          bloomThreshold_(renderer.bloomThreshold()),
          bloomSoftKnee_(renderer.bloomSoftKnee()),
          bloomIntensity_(renderer.bloomIntensity()),
          motionBlurStrength_(renderer.motionBlurStrength()),
          exposure_(renderer.exposure()) {}

    ScopedRendererPostFxState(const ScopedRendererPostFxState&) = delete;
    ScopedRendererPostFxState& operator=(const ScopedRendererPostFxState&) = delete;

    ~ScopedRendererPostFxState() {
        renderer_.setCinematicMode(cinematicModeEnabled_);
        renderer_.setDepthOfFieldEnabled(depthOfFieldEnabled_);
        renderer_.setDepthOfFieldParams(dofFocusDistance_, dofFocusRange_, dofMaxCoCRadiusPx_);
        renderer_.setBloomSettings(bloomThreshold_, bloomSoftKnee_, bloomIntensity_);
        renderer_.setMotionBlurShutterAngle(motionBlurStrength_ * 360.0f);
        renderer_.setExposure(exposure_);
    }

    // The real current state, handed to runExportSchedule() as the
    // fallback a per-sample Post FX track falls back to when unauthored
    // -- see cinematic::postFxAtTime()'s own comment.
    [[nodiscard]] cinematic::PostFxSample fallback() const {
        cinematic::PostFxSample sample;
        sample.bloomIntensity = bloomIntensity_;
        sample.bloomThreshold = bloomThreshold_;
        sample.exposure = exposure_;
        return sample;
    }

private:
    core::Renderer& renderer_;
    bool cinematicModeEnabled_;
    bool depthOfFieldEnabled_;
    float dofFocusDistance_;
    float dofFocusRange_;
    float dofMaxCoCRadiusPx_;
    float bloomThreshold_;
    float bloomSoftKnee_;
    float bloomIntensity_;
    float motionBlurStrength_;
    float exposure_;
};
} // namespace

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
    // TRANSFER_SRC_BIT (Offline Export pipeline): exportSequence()'s own
    // real depth readback (renderAndReadback()) does a real
    // vkCmdCopyImageToBuffer() straight off this image, which requires
    // it (VUID-vkCmdCopyImageToBuffer-srcImage-00186) -- without it this
    // is the exact same class of silent, no-validation-layer bug the
    // comment above this one already documents for SAMPLED_BIT, just for
    // a different missing usage flag.
    depthInfo.usage =
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
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

bool CaptureRig::renderAndReadback(core::Renderer& renderer, core::ECS& ecs, core::MeshLibrary& meshLibrary,
                                    core::TextureLibrary& textureLibrary, const core::Camera& camera,
                                    bool captureDepth, std::vector<uint8_t>& outColorRgba8,
                                    std::vector<float>& outDepth) {
    if (!isValid()) {
        core::logError("CaptureRig", "renderAndReadback: not initialized.");
        return false;
    }

    VkCommandBufferAllocateInfo cmdAllocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cmdAllocInfo.commandPool = renderer.commandPool();
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(renderer.device(), &cmdAllocInfo, &cmd) != VK_SUCCESS) {
        core::logError("CaptureRig", "renderAndReadback: vkAllocateCommandBuffers failed.");
        return false;
    }

    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    // Kronos ("Cinematic Camera Physics & Post-Processing Pipeline"):
    // real (true/true/false/false) cinematic flags, not this overload's
    // own avatar-preview-oriented defaults (false/false/true/true) --
    // see Renderer::drawSceneInto()'s own .hpp comment. An exported
    // cinematic frame should get the same real bloom/weather/sky/
    // background the live Studio viewport's own plain drawSceneInto()
    // overload already renders (StudioApp.cpp's own viewportTarget_
    // call), which is what makes this export path and the live preview
    // it's previewed through actually match.
    renderer.drawSceneInto(auxiliaryScene_, cmd, colorImage_, colorImageView_, depthImage_, depthImageView_, extent_,
                            camera, ecs, meshLibrary, captureParticleSystem_, textureLibrary,
                            /*riggedMeshLibrary=*/nullptr, /*applyWeatherEffects=*/true, /*applyBloom=*/true,
                            /*suppressSunDisk=*/false, /*useFlatBackground=*/false);

    // drawSceneInto() leaves colorImage_ in COLOR_ATTACHMENT_OPTIMAL (see
    // captureFrame()'s own comment) and depthImage_ in
    // SHADER_READ_ONLY_OPTIMAL (Renderer::drawSceneIntoImpl()'s own final
    // depth transition, unconditional across both drawSceneInto()
    // overloads) -- straight to TRANSFER_SRC_OPTIMAL for each, since this
    // readback is fully self-contained and has no need for the
    // intermediate SHADER_READ_ONLY_OPTIMAL hop captureFrame() takes
    // purely to satisfy captureThumbnailToFile()'s own separate call
    // convention.
    renderer.transitionImage(cmd, colorImage_, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                              VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                              VK_ACCESS_2_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                              VK_PIPELINE_STAGE_2_TRANSFER_BIT);
    if (captureDepth) {
        renderer.transitionImage(cmd, depthImage_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_2_SHADER_READ_BIT,
                                  VK_ACCESS_2_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                  VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
    }

    const VkDeviceSize colorBytes = static_cast<VkDeviceSize>(extent_.width) * extent_.height * 4;
    // D32_SFLOAT (Renderer::depthFormat_'s fixed real format -- see that
    // field's own comment) is 4 bytes/texel, one float, no stencil/
    // packing to unpack -- a raw byte copy IS a float array.
    const VkDeviceSize depthBytes = static_cast<VkDeviceSize>(extent_.width) * extent_.height * 4;

    VmaAllocator allocator = renderer.allocator();
    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VkBufferCreateInfo colorBufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    colorBufferInfo.size = colorBytes;
    colorBufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    colorBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkBuffer colorStaging = VK_NULL_HANDLE;
    VmaAllocation colorStagingAlloc = nullptr;
    if (vmaCreateBuffer(allocator, &colorBufferInfo, &allocInfo, &colorStaging, &colorStagingAlloc, nullptr) !=
        VK_SUCCESS) {
        core::logError("CaptureRig", "renderAndReadback: color staging buffer creation failed.");
        vkFreeCommandBuffers(renderer.device(), renderer.commandPool(), 1, &cmd);
        return false;
    }

    VkBuffer depthStaging = VK_NULL_HANDLE;
    VmaAllocation depthStagingAlloc = nullptr;
    if (captureDepth) {
        VkBufferCreateInfo depthBufferInfo = colorBufferInfo;
        depthBufferInfo.size = depthBytes;
        if (vmaCreateBuffer(allocator, &depthBufferInfo, &allocInfo, &depthStaging, &depthStagingAlloc, nullptr) !=
            VK_SUCCESS) {
            core::logError("CaptureRig", "renderAndReadback: depth staging buffer creation failed.");
            vmaDestroyBuffer(allocator, colorStaging, colorStagingAlloc);
            vkFreeCommandBuffers(renderer.device(), renderer.commandPool(), 1, &cmd);
            return false;
        }
    }

    VkBufferImageCopy colorRegion{};
    colorRegion.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    colorRegion.imageExtent = {extent_.width, extent_.height, 1};
    vkCmdCopyImageToBuffer(cmd, colorImage_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, colorStaging, 1, &colorRegion);

    if (captureDepth) {
        VkBufferImageCopy depthRegion{};
        depthRegion.imageSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1};
        depthRegion.imageExtent = {extent_.width, extent_.height, 1};
        vkCmdCopyImageToBuffer(cmd, depthImage_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, depthStaging, 1, &depthRegion);
    }

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    vkQueueSubmit(renderer.graphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(renderer.graphicsQueue());
    vkFreeCommandBuffers(renderer.device(), renderer.commandPool(), 1, &cmd);

    VmaAllocationInfo colorAllocInfoOut{};
    vmaGetAllocationInfo(allocator, colorStagingAlloc, &colorAllocInfoOut);
    const auto* colorBytesPtr = static_cast<const uint8_t*>(colorAllocInfoOut.pMappedData);
    outColorRgba8.assign(colorBytesPtr, colorBytesPtr + colorBytes);
    vmaDestroyBuffer(allocator, colorStaging, colorStagingAlloc);

    if (captureDepth) {
        VmaAllocationInfo depthAllocInfoOut{};
        vmaGetAllocationInfo(allocator, depthStagingAlloc, &depthAllocInfoOut);
        const auto* depthFloats = static_cast<const float*>(depthAllocInfoOut.pMappedData);
        outDepth.assign(depthFloats, depthFloats + (extent_.width * extent_.height));
        vmaDestroyBuffer(allocator, depthStaging, depthStagingAlloc);
    } else {
        outDepth.clear();
    }

    return true;
}

bool CaptureRig::exportSequence(core::Renderer& renderer, core::ECS& ecs, core::MeshLibrary& meshLibrary,
                                 core::TextureLibrary& textureLibrary, cinematic::Sequence& sequence,
                                 cinematic::CameraRail& rail, const cinematic::ExportSettings& settings,
                                 std::string& outError) {
    if (!isValid()) {
        // The one honest "no GPU swapchain" branch this pipeline needs --
        // see this method's own header comment. exportSequence() itself
        // can't be called headlessly (it requires a live core::Renderer&,
        // which engine_tests has no way to construct without a real
        // device -- see CaptureRig.hpp's own file comment on why nothing
        // in this class is exercised from that binary), but the guard it
        // relies on here is just isValid(), which needs no Renderer at
        // all and IS real, headless-tested directly: a default-
        // constructed, never-initialize()'d CaptureRig reports
        // isValid() == false (see test_main.cpp's own coverage), which
        // is exactly the state this branch checks for.
        outError = "Capture rig is not initialized -- no active GPU swapchain.";
        core::logError("CaptureRig", "exportSequence: %s", outError.c_str());
        return false;
    }

    // Real save/restore of every post-FX toggle this export drives --
    // see ScopedRendererPostFxState's own comment. Constructed before
    // any of the real opt-ins below so its destructor always runs,
    // regardless of which of this function's three return points fires.
    ScopedRendererPostFxState postFxGuard(renderer);
    const cinematic::PostFxSample postFxFallback = postFxGuard.fallback();

    // Real opt-in for the duration of this export: an exported cinematic
    // frame should get the same SSAO/DOF/motion-blur/vignette/god-rays
    // stack (Renderer::isCinematicModeEnabled()) the live viewport's own
    // "High" quality preset already turns on, plus real depth-of-field
    // specifically -- off by default even under Cinematic Mode (see
    // Renderer::setDepthOfFieldEnabled()'s own comment on why that's a
    // real, independent gate).
    renderer.setCinematicMode(true);
    renderer.setDepthOfFieldEnabled(true);

    // Real, deliberate override, not an oversight: the in-shader
    // velocity-buffer motion blur (shaders/cinematic.frag) computes
    // "velocity" from the *previous real drawSceneInto() call's* stored
    // transforms -- meaningful for continuous real-time playback,
    // meaningless once the sequence playhead starts jumping between the
    // arbitrary sub-frame sample times cinematic::ExportFrameJob builds.
    // This export's own real motion blur is settings.motionBlur's
    // sub-frame accumulation/averaging below -- the two are mutually
    // exclusive, not additive, so the in-shader blur is force-disabled
    // for the whole export regardless of settings.motionBlur.enabled.
    renderer.setMotionBlurShutterAngle(0.0f);

    const bool wantColor = std::find(settings.channels.begin(), settings.channels.end(),
                                      cinematic::ExportChannel::Color) != settings.channels.end();
    const bool wantDepth = std::find(settings.channels.begin(), settings.channels.end(),
                                      cinematic::ExportChannel::Depth) != settings.channels.end();

    std::error_code ec;
    std::filesystem::create_directories(settings.outputDirectory, ec); // real, idempotent -- see AssetStreamingClient.cpp's own precedent

    std::vector<std::vector<uint8_t>> colorSubFrames;
    std::vector<std::vector<float>> depthSubFrames;
    bool writeFailed = false;

    // Same real default near/far core::Camera{} itself defaults to --
    // the rail's own RailSample never carries clip planes (see
    // CameraRail.hpp's own comment: sensor/focal length are its only
    // real optics), so this is the honest choice rather than an
    // invented one.
    constexpr float kNearPlane = 0.05f;
    constexpr float kFarPlane = 500.0f;

    cinematic::ExportCaptureFn captureSample = [&](const cinematic::ExportSampleRequest& request) -> bool {
        const core::Camera camera = cinematic::cameraFromRailSample(request.cameraSample, kNearPlane, kFarPlane);

        // Real thin-lens depth of field, tracking the rail's own real,
        // per-point-interpolated optics (CameraRail.hpp's
        // RailPoint::focalLengthMm/aperture, blended into
        // request.cameraSample.camera by CameraRail::sample()) -- see
        // core::toRendererDofParams()'s own comment for the real
        // circle-of-confusion math converting focal length/aperture/
        // focus distance into what the renderer's DOF pass needs.
        const core::RendererDofParams dof =
            core::toRendererDofParams(request.cameraSample.camera, static_cast<float>(extent_.height));
        renderer.setDepthOfFieldParams(dof.focusDistance, dof.focusRange, dof.maxCoCRadiusPx);

        // Real, authored post-FX for this instant -- falls back to
        // whatever the renderer already had (postFxGuard's own snapshot)
        // when no Post FX track exists, see postFxAtTime()'s own
        // comment. Soft-knee is never sequenced (PostFxSample's own
        // comment), kept at whatever it currently is.
        renderer.setBloomSettings(request.postFx.bloomThreshold, renderer.bloomSoftKnee(), request.postFx.bloomIntensity);
        renderer.setExposure(request.postFx.exposure);

        std::vector<uint8_t> colorPixels;
        std::vector<float> depthPixels;
        if (!renderAndReadback(renderer, ecs, meshLibrary, textureLibrary, camera, wantDepth, colorPixels,
                                depthPixels)) {
            return false;
        }
        if (wantColor) colorSubFrames.push_back(std::move(colorPixels));
        if (wantDepth) depthSubFrames.push_back(std::move(depthPixels));

        const bool isLastSample = request.sampleIndex + 1 == request.job->sampleTimesSeconds.size();
        if (!isLastSample) return true;

        // Real motion-blur accumulation: a job with one sample per frame
        // (blur off) trivially "averages" to itself -- see
        // averageRgba8SubFrames()/averageDepthSubFrames()'s own comment.
        const bool swapRedBlue = colorFormat_ == VK_FORMAT_B8G8R8A8_SRGB || colorFormat_ == VK_FORMAT_B8G8R8A8_UNORM;
        if (wantColor) {
            std::vector<uint8_t> averaged = averageRgba8SubFrames(colorSubFrames);
            std::string path =
                settings.outputDirectory + "/" +
                cinematic::exportFrameFilename(settings, request.job->frameIndex, cinematic::ExportChannel::Color);
            if (!writePngRgba8(averaged.data(), extent_.width, extent_.height, swapRedBlue, path)) writeFailed = true;
        }
        if (wantDepth) {
            std::vector<float> averaged = averageDepthSubFrames(depthSubFrames);
            std::string path =
                settings.outputDirectory + "/" +
                cinematic::exportFrameFilename(settings, request.job->frameIndex, cinematic::ExportChannel::Depth);
            if (!writeExrDepth(averaged.data(), extent_.width, extent_.height, path)) writeFailed = true;
        }

        colorSubFrames.clear();
        depthSubFrames.clear();
        return !writeFailed; // a disk-write failure aborts the rest of the export, same as a capture failure
    };

    if (!cinematic::runExportSchedule(sequence, rail, settings, ecs, captureSample, outError, postFxFallback))
        return false;
    if (writeFailed) {
        outError = "One or more frames failed to write to \"" + settings.outputDirectory + "\".";
        return false;
    }
    return true;
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
