#include "core/Renderer.hpp"
#include "core/render/RendererInternal.hpp"
#include "core/SceneSpatialIndex.hpp"
#include "core/StaticBatching.hpp"
#include "core/GpuFeatures.hpp"

#include "core/CascadeSplitMath.hpp"
#include "core/Hierarchy.hpp"
#include "core/ResourcePaths.hpp"
#include "core/SwapchainSelection.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <set>
#include <unordered_map>


namespace engine::core {

using namespace renderer_internal;

void Renderer::drawSSRPass(VkCommandBuffer cmd, FrameSync& frame, VkExtent2D extent) {
    // Real, direct bypass -- see this method's own header comment in
    // Renderer.hpp. Nothing below records so much as a barrier when SSR
    // is off.
    if (!ssrEnabled_) return;

    // Kronos ("Reflection Fix" -- live-reported "flickering double
    // reflections on the player and baseplate"): real root cause --
    // scene_rt.frag's own RT reflection block already gives every opaque
    // pixel a real reflection contribution when it's active this frame,
    // smoothly blending from a full ray-traced reflection on mirror-
    // smooth surfaces down to a cheap analytic sky reflection on rough
    // ones (see its own reflectionParams.x/.y/.z comment) -- there is no
    // gap left for this pass to fill the way this class's own
    // setSSREnabled() comment (and main.cpp's showcase setup) describe.
    // Running this pass on top anyway re-reflects an already-reflected
    // pixel a second time; since this pass's screen-space raymarch hit
    // point is inherently unstable frame-to-frame (no G-buffer to sample
    // a stable per-pixel normal from, see this file's own header
    // comment) while the RT contribution underneath it is stable, the
    // two combined is exactly what reads as flickering double
    // reflections, not two independent bugs. RT reflections strictly
    // supersede this pass's own coarser fallback whenever they're
    // actually active -- checked per-frame (not via ssrEnabled_ itself)
    // so a scene with RT reflections requested but no valid TLAS yet
    // still gets real SSR as the honest fallback it's meant to be.
    if (rtReflectionsEnabled_ && frame.rayTracing.tlas != VK_NULL_HANDLE) return;
    if (frame.ssrImage == VK_NULL_HANDLE || frame.ssrInputDescriptorSet == VK_NULL_HANDLE) {
        // ensureSSRTargets() failed this frame (already logged there) --
        // skip rather than recording a draw against a null target;
        // frame.hdrImage still flows into whatever's next unchanged
        // (ssrSourceBoundForComposite only flips once the target exists,
        // see ensureCinematicTarget()'s own tri-state -> 4-way logic).
        return;
    }

    transitionImage(cmd, frame.ssrImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                     VK_ACCESS_2_NONE, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
                     VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);

    VkRenderingAttachmentInfo colorAttachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    colorAttachment.imageView = frame.ssrView;
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; // fully overwritten every pixel
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingInfo renderingInfo{VK_STRUCTURE_TYPE_RENDERING_INFO};
    renderingInfo.renderArea = {{0, 0}, extent};
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;

    vkCmdBeginRendering(cmd, &renderingInfo);
    VkViewport viewport{0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f};
    VkRect2D scissor{{0, 0}, extent};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ssrPipeline_);
    std::array<VkDescriptorSet, 2> sets{frame.sceneDescriptorSet, frame.ssrInputDescriptorSet};
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ssrPipelineLayout_, 0,
                             static_cast<uint32_t>(sets.size()), sets.data(), 0, nullptr);

    SSRPushConstants push{};
    push.maxDistance = ssrMaxDistance_;
    push.thickness = ssrThickness_;
    push.stepCount = ssrStepCount_;
    vkCmdPushConstants(cmd, ssrPipelineLayout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    recordDraw(3, 1);

    vkCmdEndRendering(cmd);

    transitionImage(cmd, frame.ssrImage, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                     VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_2_SHADER_READ_BIT,
                     VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);
}

void Renderer::drawVolumetricFogPass(VkCommandBuffer cmd, FrameSync& frame, VkExtent2D extent) {
    // Real, direct bypass -- see this method's own header comment in
    // Renderer.hpp. Nothing below records so much as a barrier when
    // volumetric fog is off.
    if (!volumetricFogEnabled_) return;
    if (frame.fogImage == VK_NULL_HANDLE || frame.fogInputDescriptorSet == VK_NULL_HANDLE) {
        // ensureVolumetricFogTargets() failed this frame (already logged
        // there) -- skip rather than recording a draw against a null
        // target; frame.hdrImage still flows into whatever's next
        // unchanged (fogSourceBoundForComposite only flips once the
        // target exists, see ensureCinematicTarget()'s own tri-state logic).
        return;
    }

    transitionImage(cmd, frame.fogImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                     VK_ACCESS_2_NONE, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
                     VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);

    VkRenderingAttachmentInfo colorAttachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    colorAttachment.imageView = frame.fogView;
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; // fully overwritten every pixel
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingInfo renderingInfo{VK_STRUCTURE_TYPE_RENDERING_INFO};
    renderingInfo.renderArea = {{0, 0}, extent};
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;

    vkCmdBeginRendering(cmd, &renderingInfo);
    VkViewport viewport{0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f};
    VkRect2D scissor{{0, 0}, extent};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, volumetricFogPipeline_);
    std::array<VkDescriptorSet, 2> sets{frame.sceneDescriptorSet, frame.fogInputDescriptorSet};
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, volumetricFogPipelineLayout_, 0,
                             static_cast<uint32_t>(sets.size()), sets.data(), 0, nullptr);

    VolumetricFogPushConstants push{};
    push.scatteringIntensity = volumetricFogScatteringIntensity_;
    push.maxDistance = volumetricFogMaxDistance_;
    push.stepCount = volumetricFogStepCount_;
    push.ambientFogContribution = volumetricFogAmbientContribution_;
    push.groundDensityMultiplier = volumetricFogGroundDensityMultiplier_;
    push.aloftDensityMultiplier = volumetricFogAloftDensityMultiplier_;
    push.groundHeightY = volumetricFogGroundHeightY_;
    push.falloffHeight = volumetricFogFalloffHeight_;
    vkCmdPushConstants(cmd, volumetricFogPipelineLayout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    recordDraw(3, 1);

    vkCmdEndRendering(cmd);

    transitionImage(cmd, frame.fogImage, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                     VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_2_SHADER_READ_BIT,
                     VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);
}

void Renderer::drawCinematicPass(VkCommandBuffer cmd, FrameSync& frame, VkExtent2D extent) {
    // Real, direct bypass -- see this method's own header comment in
    // Renderer.hpp. Nothing below records so much as a barrier when
    // Cinematic Mode is off.
    if (!cinematicModeEnabled_) return;
    if (frame.cinematicImage == VK_NULL_HANDLE || frame.cinematicDescriptorSet == VK_NULL_HANDLE ||
        frame.motionBlurBinding.sampleSet == VK_NULL_HANDLE) {
        // ensureCinematicTarget() failed this frame (already logged
        // there, e.g. descriptor pool exhaustion) -- skip rather than
        // recording a draw against a null target; frame.hdrImage still
        // flows into bloom_extract unchanged (cinematicSourceBound only
        // flips once the target exists, see ensureCinematicTarget()).
        return;
    }

    // Sprint 16 auto-exposure: read back *this slot's own previous*
    // luminance measurement (real, stall-free -- guaranteed complete by
    // the same per-slot vkWaitForFences() that already gates reuse of
    // every other persistently-mapped FrameSync buffer, e.g.
    // sceneUboMapped; see FrameSync::luminanceReadbackMapped's own
    // comment), then temporally adapt frame.autoExposureValue toward the
    // exposure that measurement implies. This frame's *own* new
    // measurement is rendered at the bottom of this function, for the
    // *next* time this slot is used to read back.
    if (frame.luminanceReadbackMapped != nullptr) {
        float measuredLuminance = *static_cast<float*>(frame.luminanceReadbackMapped);
        float targetExposure = kAutoExposureTargetLuminance / std::max(measuredLuminance, 0.01f);
        frame.autoExposureValue += (targetExposure - frame.autoExposureValue) * kAutoExposureAdaptRate;
        frame.autoExposureValue = std::clamp(frame.autoExposureValue, 0.05f, 20.0f);
    }

    motionBlur_.prepareTiles(cmd, frame.motionBlurBinding, motionBlurStrength_ > 0.001f);
    transitionImage(cmd, frame.cinematicImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                     VK_ACCESS_2_NONE, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
                     VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);

    VkRenderingAttachmentInfo colorAttachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    colorAttachment.imageView = frame.cinematicView;
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; // fully overwritten every pixel
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingInfo renderingInfo{VK_STRUCTURE_TYPE_RENDERING_INFO};
    renderingInfo.renderArea = {{0, 0}, extent};
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;

    vkCmdBeginRendering(cmd, &renderingInfo);
    VkViewport viewport{0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f};
    VkRect2D scissor{{0, 0}, extent};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, cinematicPipeline_);
    std::array<VkDescriptorSet, 3> sets{frame.sceneDescriptorSet, frame.cinematicDescriptorSet,
                                        frame.motionBlurBinding.sampleSet};
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, cinematicPipelineLayout_, 0,
                             static_cast<uint32_t>(sets.size()), sets.data(), 0, nullptr);

    CinematicPushConstants push{};
    push.focusDistance = dofFocusDistance_;
    push.focusRange = dofFocusRange_;
    push.maxCoCRadiusPx = dofMaxCoCRadiusPx_;
    push.motionBlurStrength = motionBlurStrength_;
    push.ssaoRadius = ssaoRadius_;
    push.ssaoStrength = ssaoStrength_;
    // Kronos ("Critical Visual Fixes" -- "High Quality Graphics
    // Blurriness"): real -- was unconditionally 1.0f, so any Cinematic
    // Mode caller (including RuntimeShell's own "High" quality preset,
    // which never tunes DOF for gameplay distances) got the class-default
    // DOF params applied full-strength. See setDepthOfFieldEnabled()'s
    // own comment.
    push.dofEnabled = depthOfFieldEnabled_ ? 1.0f : 0.0f;
    push.ssaoEnabled = 1.0f;
    push.heatDistortionStrength = heatDistortionEnabled_ ? heatDistortionStrength_ : 0.0f;
    push.time = totalElapsedTimeSeconds_;
    vkCmdPushConstants(cmd, cinematicPipelineLayout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    recordDraw(3, 1);

    vkCmdEndRendering(cmd);

    transitionImage(cmd, frame.cinematicImage, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                     VK_ACCESS_2_SHADER_READ_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                     VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);

    drawLuminancePass(cmd, frame);
}

// Sprint 16 auto-exposure -- renders this frame's real average-luminance
// measurement (shaders/luminance.frag, sourced from frame.hdrImage, which
// is already SHADER_READ_ONLY_OPTIMAL by this point -- see
// drawSceneIntoImpl()'s own transition) into frame.luminanceImage, then
// copies it into frame.luminanceReadbackBuffer for drawCinematicPass() to
// read back the *next* time this slot is used (see that read site's own
// comment on why that's stall-free).
void Renderer::drawLuminancePass(VkCommandBuffer cmd, FrameSync& frame) {
    if (frame.luminanceImage == VK_NULL_HANDLE || frame.luminanceDescriptorSet == VK_NULL_HANDLE) return;

    transitionImage(cmd, frame.luminanceImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                     VK_ACCESS_2_NONE, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
                     VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);

    VkRenderingAttachmentInfo attachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    attachment.imageView = frame.luminanceView;
    attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkExtent2D oneByOne{1, 1};
    VkRenderingInfo renderingInfo{VK_STRUCTURE_TYPE_RENDERING_INFO};
    renderingInfo.renderArea = {{0, 0}, oneByOne};
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &attachment;

    vkCmdBeginRendering(cmd, &renderingInfo);
    VkViewport viewport{0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f};
    VkRect2D scissor{{0, 0}, oneByOne};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, luminancePipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, luminancePipelineLayout_, 0, 1,
                             &frame.luminanceDescriptorSet, 0, nullptr);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    recordDraw(3, 1);

    vkCmdEndRendering(cmd);

    transitionImage(cmd, frame.luminanceImage, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                     VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
                     VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT);

    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {1, 1, 1};
    vkCmdCopyImageToBuffer(cmd, frame.luminanceImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, frame.luminanceReadbackBuffer,
                            1, &region);
}

void Renderer::drawBloomAndComposite(VkCommandBuffer cmd, FrameSync& frame, VkImage colorImage, VkImageView colorView,
                                      VkExtent2D extent, glm::vec2 sunScreenUV, bool sunVisible, bool applyBloom) {
    // frame.hdrImage (and, if Cinematic Mode ran this frame,
    // frame.cinematicImage) is already in SHADER_READ_ONLY_OPTIMAL by the
    // time this runs -- see drawSceneIntoImpl()'s own transition, done
    // once there rather than duplicated here and in drawCinematicPass().
    VkExtent2D bloomExtent = frame.postProcessExtent; // set by ensurePostProcessTargets(); recompute the half-res size to match
    bloomExtent.width = std::max(1u, static_cast<uint32_t>(static_cast<float>(extent.width) * kBloomDownsampleFactor));
    bloomExtent.height = std::max(1u, static_cast<uint32_t>(static_cast<float>(extent.height) * kBloomDownsampleFactor));

    // --- Bloom extract: frame.hdrImage (read) -> frame.bloomImage (write) ---
    transitionImage(cmd, frame.bloomImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                     VK_ACCESS_2_NONE, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
                     VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);

    VkRenderingAttachmentInfo bloomAttachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    bloomAttachment.imageView = frame.bloomView;
    bloomAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    bloomAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingInfo bloomRenderingInfo{VK_STRUCTURE_TYPE_RENDERING_INFO};
    bloomRenderingInfo.renderArea = {{0, 0}, bloomExtent};
    bloomRenderingInfo.layerCount = 1;
    bloomRenderingInfo.colorAttachmentCount = 1;
    bloomRenderingInfo.pColorAttachments = &bloomAttachment;

    // Kronos ("Avatar Preview Rendering" pre-launch fix): real bloom
    // bleed, not the sky/lighting values themselves, was washing out
    // close-up preview renders (HomeAvatarPreview's "Your Avatar" box,
    // Studio's AvatarEditor/CataloguePanel/etc.) -- confirmed the actual
    // UBO sky colors reach the shader correctly (verified live via a
    // temporary debug print), so the remaining candidate was
    // post-process: bloomThreshold_'s real default (1.0, HDR linear)
    // is easily exceeded by a well-lit close-up subject under this
    // preset's own 2.6-intensity key light, and a tight preview frame
    // where the subject fills most of the image has proportionally far
    // more of its area affected by that bloom bleed than a normal
    // full-scene outdoor shot would. `applyBloom=false` for every
    // auxiliary/preview scene (see drawSceneInto()'s AuxiliarySceneHandle
    // overload) reuses this exact same real, already-proven
    // "performanceModeEnabled_" zero-bloom path -- a real "product
    // photography lightbox" backdrop shouldn't show HDR bloom blowout at
    // all, independent of whether this fully explains every washed-out
    // report.
    if (performanceModeEnabled_ || !applyBloom) {
        // Sprint 14 ("Performance Mode"): a real clear (VK_ATTACHMENT_LOAD_OP_CLEAR,
        // no draw call at all) instead of running bloomExtractPipeline_'s
        // real fragment shader across every bloom-res pixel -- a real,
        // direct GPU cost cut, not just a visual no-op (setBloomSettings'
        // intensity=0 would still pay the full extract-shader cost for
        // zero visible result; this skips the shader entirely). The
        // composite pass below reads this same real, now-black
        // frame.bloomImage unchanged, contributing zero bloom.
        bloomAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        bloomAttachment.clearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
        vkCmdBeginRendering(cmd, &bloomRenderingInfo);
        vkCmdEndRendering(cmd);
    } else {
        bloomAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; // fully overwritten every pixel -- no need to clear or preserve

        vkCmdBeginRendering(cmd, &bloomRenderingInfo);
        VkViewport bloomViewport{0.0f, 0.0f, static_cast<float>(bloomExtent.width),
                                  static_cast<float>(bloomExtent.height), 0.0f, 1.0f};
        VkRect2D bloomScissor{{0, 0}, bloomExtent};
        vkCmdSetViewport(cmd, 0, 1, &bloomViewport);
        vkCmdSetScissor(cmd, 0, 1, &bloomScissor);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, bloomExtractPipeline_);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, bloomExtractPipelineLayout_, 0, 1,
                                 &frame.bloomExtractDescriptorSet, 0, nullptr);
        BloomPushConstants bloomPush{bloomThreshold_, bloomSoftKnee_};
        vkCmdPushConstants(cmd, bloomExtractPipelineLayout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(bloomPush), &bloomPush);
        vkCmdDraw(cmd, 3, 1, 0, 0); // full-screen triangle -- no vertex/index buffer bound, see shaders/fullscreen.vert
        recordDraw(3, 1);

        vkCmdEndRendering(cmd);
    }

    transitionImage(cmd, frame.bloomImage, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                     VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_2_SHADER_READ_BIT,
                     VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);

    // --- Composite: frame.hdrImage + frame.bloomImage (read) -> colorImage (write) ---
    transitionImage(cmd, colorImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_2_NONE,
                     VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
                     VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);

    VkRenderingAttachmentInfo colorAttachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    colorAttachment.imageView = colorView;
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingInfo compositeRenderingInfo{VK_STRUCTURE_TYPE_RENDERING_INFO};
    compositeRenderingInfo.renderArea = {{0, 0}, extent};
    compositeRenderingInfo.layerCount = 1;
    compositeRenderingInfo.colorAttachmentCount = 1;
    compositeRenderingInfo.pColorAttachments = &colorAttachment;

    vkCmdBeginRendering(cmd, &compositeRenderingInfo);
    VkViewport viewport{0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f};
    VkRect2D scissor{{0, 0}, extent};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, compositePipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, compositePipelineLayout_, 0, 1,
                             &frame.compositeDescriptorSet, 0, nullptr);
    CompositePushConstants compositePush{};
    // Sprint 16: with Cinematic Mode on, this slot's own real, temporally-
    // adapted auto-exposure value (see drawCinematicPass()'s readback)
    // drives exposure instead of the flat manual exposure_ -- "dynamic
    // exposure (auto-exposure curve)" from the brief. With it off, this
    // composite pass stays byte-for-byte the pre-Sprint-16 result
    // (vignette/CA/god-rays at their real zero-effect value, saturation
    // at 1.0 == no-op mix()).
    compositePush.exposure = (cinematicModeEnabled_ && autoExposureEnabled_) ? frame.autoExposureValue : exposure_;
    compositePush.bloomIntensity = bloomIntensity_;
    if (cinematicModeEnabled_) {
        compositePush.vignetteStrength = vignetteStrength_;
        compositePush.chromaticAberrationStrength = chromaticAberrationStrength_;
        compositePush.saturation = saturation_;
        compositePush.godRayStrength = godRayStrength_;
        compositePush.sunScreenX = sunScreenUV.x;
        compositePush.sunScreenY = sunScreenUV.y;
        compositePush.sunVisible = sunVisible ? 1.0f : 0.0f;
    } else {
        compositePush.saturation = 1.0f;
    }
    // Kronos ("Settings Panel v2 + Input Remapping + Accessibility
    // Layer" -- "Accessibility: Colorblind modes"): real, applies
    // regardless of Cinematic Mode -- colorblind correction is an
    // accessibility need, not a cosmetic grading choice, so it stays
    // active even with every other composite-pass effect at its own
    // real, cinematic-mode-off zero value.
    compositePush.colorblindMode = static_cast<float>(colorblindModeIndex_);
    // Kronos ("Cinematic Camera Physics & Post-Processing Pipeline"):
    // real, applies regardless of Cinematic Mode, same reasoning as
    // colorblindMode above -- the tonemap curve is a real display
    // transform every frame needs, not a cinematic-only effect.
    compositePush.tonemapOperator = tonemapOperator_ == TonemapOperator::AgX ? 1.0f : 0.0f;
    compositePush.lutStrength = lutStrength_;
    // Kronos ("VHS / Analog Bodycam"): also applies regardless of
    // Cinematic Mode -- see setVhsBodycamSettings()'s own comment on why
    // this is DESPAIR's visual identity, not a graphics-quality knob, and
    // every other scene leaves all three at their real zero-effect default.
    compositePush.fisheyeStrength = fisheyeStrength_;
    compositePush.scanlineIntensity = scanlineIntensity_;
    compositePush.staticNoiseIntensity = staticNoiseIntensity_;
    compositePush.time = totalElapsedTimeSeconds_;
    vkCmdPushConstants(cmd, compositePipelineLayout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(compositePush),
                        &compositePush);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    recordDraw(3, 1);

    vkCmdEndRendering(cmd);
    // colorImage left in COLOR_ATTACHMENT_OPTIMAL -- same contract
    // drawSceneInto() has always had; the caller (renderFrame() or
    // StudioApp's prePassCallback) transitions it onward from here.
}

void Renderer::transitionImage(VkCommandBuffer cmd, VkImage image,
                                VkImageLayout oldLayout, VkImageLayout newLayout,
                                VkAccessFlags2 srcAccess, VkAccessFlags2 dstAccess,
                                VkPipelineStageFlags2 srcStage, VkPipelineStageFlags2 dstStage,
                                VkImageAspectFlags aspect, uint32_t layerCount) const {
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
    barrier.subresourceRange = {aspect, 0, 1, 0, layerCount};

    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(cmd, &dep);
}

void Renderer::recordDraw(uint32_t indexOrVertexCount, uint32_t instanceCount) {
    frameDrawCalls_ += 1;
    frameTriangles_ += (static_cast<uint64_t>(indexOrVertexCount) / 3) * instanceCount;
}

void Renderer::drawSceneIntoImpl(FrameSync& frame, VkCommandBuffer cmd, VkImage colorImage, VkImageView colorView,
                                  VkImage depthImage, VkImageView depthView, VkExtent2D extent, const Camera& camera,
                                  ECS& ecs, MeshLibrary& meshLibrary, const ParticleSystem& particleSystem,
                                  TextureLibrary& textureLibrary, RiggedMeshLibrary* riggedMeshLibrary,
                                  bool applyWeatherEffects, bool applyBloom, bool suppressSunDisk,
                                  bool useFlatBackground) {
    // (Re)creates frame.hdrImage/bloomImage if `extent` changed since the
    // last call -- see FrameSync's comment and OffscreenTarget::ensureSize()
    // for the same lazy-resize pattern applied elsewhere in this codebase.
    if (!ensurePostProcessTargets(frame, extent, depthView)) {
        std::fprintf(stderr, "Renderer: ensurePostProcessTargets failed -- skipping this frame's scene draw.\n");
        return;
    }

    float aspectRatio = static_cast<float>(extent.width) / static_cast<float>(extent.height);

    const bool rayTracingActive = rayTracingSupported_ && rayTracingWanted() && frame.rayTracing.tlas != VK_NULL_HANDLE;
    if (rayTracingActive) {
        recordRayTracingScene(cmd, frame, ecs, meshLibrary, textureLibrary, riggedMeshLibrary);
    } else {
        lastRtInstanceCount_ = 0;
    }

    // The UBO (including every cascade's lightViewProj) is filled once,
    // before either pass runs: the shadow pass's vertex shader indexes
    // lightViewProj[cascadeIndex], and the main pass's fragment shader
    // needs the whole array too (to re-derive the right cascade's
    // light-space position for shadow sampling) -- both read it through
    // the same descriptor set, so there's exactly one write, not one per pass.
    render::CascadeFits cascades = computeCascades(camera, aspectRatio);
    render::ViewHistory& history = *frame.viewHistory;

    SceneUBO ubo{};
    ubo.view = camera.viewMatrix();
    ubo.proj = camera.projectionMatrix(aspectRatio);
    ubo.viewProjNoJitter = ubo.proj * ubo.view;
    ubo.prevViewProjNoJitter = history.hasPreviousViewProj ? history.previousViewProjNoJitter : ubo.viewProjNoJitter;

    const bool taaActive = temporalAAEnabled_ && depthImage != VK_NULL_HANDLE;
    glm::vec2 jitter(0.0f);
    if (taaActive) {
        jitter = render::TemporalAA::jitterNdc(history.frameIndex, extent);
        // Offsetting the projection's z column shifts clip-space x/y by jitter * w, i.e. a constant NDC offset.
        ubo.proj[2][0] += jitter.x;
        ubo.proj[2][1] += jitter.y;
    }
    ubo.invViewProj = glm::inverse(ubo.proj * ubo.view);
    ubo.taaJitter = glm::vec4(jitter, taaActive ? history.previousJitter : glm::vec2(0.0f));
    ubo.screenSize = glm::vec4(static_cast<float>(extent.width), static_cast<float>(extent.height),
                               1.0f / static_cast<float>(extent.width), 1.0f / static_cast<float>(extent.height));
    ubo.frameParams = glm::vec4(static_cast<float>(history.frameIndex % 1024), taaActive ? 1.0f : 0.0f, 0.0f, 0.0f);

    for (uint32_t cascade = 0; cascade < kCascadeCount; ++cascade) {
        const render::CascadeFit& fit = cascades[cascade];
        ubo.lightViewProj[cascade] = fit.viewProj;
        ubo.cascadeSplitsView[static_cast<glm::length_t>(cascade)] = fit.splitFar;
        ubo.cascadeTexelWorld[static_cast<glm::length_t>(cascade)] = fit.texelWorld;
        ubo.cascadeDepthRange[static_cast<glm::length_t>(cascade)] = fit.depthRange;
    }
    ubo.shadowParams = glm::vec4(receiverPlaneBiasScale_, std::tan(glm::radians(sunAngularRadiusDegrees_)),
                                 static_cast<float>(kShadowMapResolution), performanceModeEnabled_ ? 1.0f : 2.0f);
    // Kronos ("Rendering Fidelity Foundation" Phase 1.1): real weather
    // composed on top of whatever setLighting() last provided -- see
    // applyWeather()'s own comment for why Clear weather is an exact,
    // zero-cost no-op here (`effectiveLighting` is just `lighting_` by
    // reference-equal value in that case). Every caller of setLighting()
    // gets weather "for free," including this same struct's own
    // WeatherProfile::wetness feeding renderFlags.z below.
    //
    // Kronos ("Avatar Scene Lighting Calibration Pass"): `applyWeatherEffects`
    // (false for every auxiliary/preview scene, see this function's own
    // header comment) real-substitutes `weatherProfileFor(WeatherKind::Clear)`
    // in place of the outdoor world's own current blended weather --
    // `applyWeather()` already treats Clear as a real, exact identity, so
    // this is a genuine no-op for preview scenes, not an approximation.
    WeatherProfile weatherProfile =
        applyWeatherEffects ? currentBlendedProfile(weatherState_) : weatherProfileFor(WeatherKind::Clear);
    SceneLighting effectiveLighting = applyWeather(lighting_, weatherProfile);
    ubo.lightDirectionWS = glm::vec4(glm::normalize(effectiveLighting.directionWS), 0.0f);
    ubo.lightColorIntensity = glm::vec4(effectiveLighting.color, effectiveLighting.intensity);
    ubo.viewPositionWS = glm::vec4(camera.position, 1.0f);
    ubo.ambientColor = glm::vec4(effectiveLighting.ambient, 0.0f);
    ubo.ambientGroundColor = glm::vec4(effectiveLighting.ambientGround, 0.0f);
    ubo.fogColorDensity = glm::vec4(effectiveLighting.fogColor, effectiveLighting.fogDensity);
    ubo.skyZenithColor = glm::vec4(effectiveLighting.skyZenithColor, 0.0f);
    ubo.skyHorizonColor = glm::vec4(effectiveLighting.skyHorizonColor, 0.0f);
    // Sprint 14: real-true only with BOTH the user's own toggle on AND a
    // real, currently non-empty TLAS -- see this field's own comment in
    // SceneTypes.hpp. scene_rt.frag never attempts a real ray query
    // against a real, valid-but-empty (zero-instance) TLAS just because
    // the user flipped the toggle before any real shadow-caster existed.
    ubo.renderFlags.x = (rayTracingActive && rayTracedShadowsEnabled_) ? 1.0f : 0.0f;
    ubo.renderFlags.y = performanceModeEnabled_ ? 1.0f : 0.0f;
    // Kronos ("Rendering Fidelity Foundation" Phase 1.1): real
    // wet-surface response -- see WeatherProfile::wetness's own comment.
    // 0 at Clear (weatherProfile.overrideStrength == 0 forces wetness
    // toward Clear's own 0.0 as any transition settles, see
    // currentBlendedProfile()), so this is a real no-op end-state, not
    // just a currently-unused slot. z was documented std140 padding
    // before this -- w remains real, unused padding.
    ubo.renderFlags.z = std::clamp(weatherProfile.wetness, 0.0f, 1.0f);
    // Kronos ("Four RTX Maps" Phase 5c): real underwater caustic-light
    // strength -- see setUnderwaterCausticsEnabled()'s own comment. 0 (the
    // default, every non-Underwater map) is a real, exact identity in both
    // scene.frag and scene_rt.frag's own matching hook. w was documented
    // std140 padding before this.
    ubo.renderFlags.w = underwaterCausticsEnabled_ ? 1.0f : 0.0f;

    render::gatherLights(ecs, lighting_, camera.position, gatheredLights_);
    const render::SpotShadowSet spotShadows =
        render::assignSpotShadows(gatheredLights_, ubo.viewProjNoJitter, camera.position);
    std::copy(spotShadows.viewProj.begin(), spotShadows.viewProj.end(), ubo.spotShadowViewProj);
    ubo.spotShadowTexelScale = spotShadows.texelScale;
    uint32_t lightCount = render::ClusteredLighting::uploadLights(frame.clusterResources, gatheredLights_);
    render::ClusterGrid clusterGrid = render::computeClusterGrid(extent.width, extent.height, camera.nearPlane,
                                                                 camera.farPlane);
    ubo.clusterParams = glm::vec4(clusterGrid.sliceScale, clusterGrid.sliceBias, clusterGrid.tileSizePixels,
                                  static_cast<float>(lightCount));
    ubo.clusterDims = glm::uvec4(clusterGrid.dims, kMaxLightsPerCluster);

    ubo.iblParams = glm::vec4(iblSpecularIntensity_, iblReflectionNormalization_, ibl_.prefilteredMaxMip(), 0.0f);
    // Kronos ("Rendering Fidelity Foundation" Phase 1.3): same real
    // "toggle AND a currently-valid TLAS" gating as renderFlags.x above --
    // see this field's own comment in SceneTypes.hpp.
    ubo.reflectionParams.x = (rayTracingActive && rtReflectionsEnabled_) ? 1.0f : 0.0f;
    ubo.reflectionParams.y = reflectionRoughCutoff_;
    ubo.reflectionParams.z = reflectionMirrorCutoff_;
    // Kronos ("Rendering Fidelity" -- full atmospheric-scattering skybox):
    // see SceneUBO::atmosphereParams's own comment.
    ubo.atmosphereParams.x = atmosphereScatteringEnabled_ ? 1.0f : 0.0f;
    ubo.atmosphereParams.y = atmosphereSunIntensity_;
    ubo.atmosphereParams.z = atmosphereMieStrength_;
    // Kronos ("Avatar Preview Rendering" pre-launch fix): real, previously
    // spare .w slot -- see shaders/sky.frag's own comment on why every
    // auxiliary/preview scene suppresses the sun disk/glow entirely.
    ubo.atmosphereParams.w = suppressSunDisk ? 1.0f : 0.0f;
    // Kronos ("Rendering Fidelity" -- volumetric cloud layer): see
    // SceneUBO::cloudParams's own comment.
    ubo.cloudParams.x = cloudsEnabled_ ? 1.0f : 0.0f;
    ubo.cloudParams.y = cloudCoverage_;
    ubo.cloudParams.z = cloudSpeed_;
    ubo.cloudParams.w = totalElapsedTimeSeconds_;
    // Kronos ("Rendering Fidelity" -- ray-traced bounce lighting/GI): see
    // SceneUBO::giParams's own comment. Same real "toggle AND a
    // currently-valid TLAS" gating renderFlags.x/reflectionParams.x above
    // already use.
    ubo.giParams.x = (rayTracingActive && rtGIEnabled_) ? 1.0f : 0.0f;
    ubo.giParams.y = rtGIIntensity_;
    ubo.giParams.z = static_cast<float>(performanceModeEnabled_ ? std::max(1, rtGISamples_ / 2) : rtGISamples_);
    ubo.giParams.w = (rayTracingActive && rtAOEnabled_) ? rtAORadius_ : 0.0f;

    // The environment is captured from the same analytic sky the sky pass draws, so reflections and the
    // visible background agree; it is only re-captured when the quantised sky inputs change.
    render::IblInputs iblInputs;
    iblInputs.zenith = effectiveLighting.skyZenithColor;
    iblInputs.horizon = effectiveLighting.skyHorizonColor;
    iblInputs.ground = effectiveLighting.ambientGround;
    iblInputs.towardSun = -glm::vec3(ubo.lightDirectionWS);
    iblInputs.origin = camera.position;
    iblInputs.atmosphere = ubo.atmosphereParams;
    iblInputs.clouds = ubo.cloudParams;
    ibl_.update(cmd, iblInputs);
    ubo.iblParams.w = ibl_.valid() ? 1.0f : 0.0f;

    std::memcpy(frame.sceneUboMapped, &ubo, sizeof(ubo)); // persistently mapped -- no map/unmap round trip

    prepareSkinnedDraws(frame, ecs, riggedMeshLibrary);
    activeBatching_ = staticBatching_ ? prepareStaticBatches(ecs, meshLibrary) : nullptr;
    drawShadowPass(cmd, frame, ecs, meshLibrary, cascades, spotShadows);
    clusteredLighting_.record(cmd, frame.sceneDescriptorSet, frame.clusterResources, clusterGrid);
    frame.objectRecordCount = 1;

    // Renders into frame.hdrImage (the internal HDR intermediate), not
    // colorImage directly -- drawBloomAndComposite() at the end of this
    // function reads frame.hdrImage back and writes the caller's
    // colorImage/colorView as its own final step. See this method's
    // declaration comment.
    transitionImage(cmd, frame.hdrImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                     VK_ACCESS_2_NONE, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                     VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                     VK_IMAGE_ASPECT_COLOR_BIT);
    transitionImage(cmd, depthImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                     VK_ACCESS_2_NONE, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                     VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
                     VK_IMAGE_ASPECT_DEPTH_BIT);
    render::imageBarrier(cmd, frame.velocity.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                         VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, 0, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

    VkRenderingAttachmentInfo colorAttachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    colorAttachment.imageView = frame.hdrView;
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    // Kronos ("Avatar Preview Rendering" pre-launch fix -- direct,
    // guaranteed hardware-level override, not another indirect shader
    // tuning pass): every auxiliary/preview scene (useFlatBackground)
    // gets a real, fixed dark-slate clear color here, and the sky pass
    // below is skipped for it entirely -- no sky.frag draw call at all,
    // so nothing (gradient, sun disk, atmosphere, clouds, any future sky
    // feature) can ever paint over this clear value for these scenes.
    // The main viewport is completely unaffected (useFlatBackground
    // stays false there, exact prior behavior).
    colorAttachment.clearValue.color =
        useFlatBackground ? VkClearColorValue{{0.08f, 0.09f, 0.13f, 1.0f}} : VkClearColorValue{{0.04f, 0.045f, 0.06f, 1.0f}};

    VkRenderingAttachmentInfo depthAttachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    depthAttachment.imageView = depthView;
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    // STORE, not DONT_CARE: the soft-particle pass below samples this
    // depth after the opaque pass ends, so its contents must survive.
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.clearValue.depthStencil = {1.0f, 0};

    VkRenderingInfo renderingInfo{VK_STRUCTURE_TYPE_RENDERING_INFO};
    renderingInfo.renderArea = {{0, 0}, extent};
    renderingInfo.layerCount = 1;
    VkRenderingAttachmentInfo velocityAttachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    velocityAttachment.imageView = frame.velocity.view;
    velocityAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    velocityAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    velocityAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    velocityAttachment.clearValue.color = VkClearColorValue{{0.0f, 0.0f, 0.0f, 0.0f}};
    std::array<VkRenderingAttachmentInfo, 2> sceneColorAttachments{colorAttachment, velocityAttachment};

    renderingInfo.colorAttachmentCount = static_cast<uint32_t>(sceneColorAttachments.size());
    renderingInfo.pColorAttachments = sceneColorAttachments.data();
    renderingInfo.pDepthAttachment = &depthAttachment;

    vkCmdBeginRendering(cmd, &renderingInfo);

    VkViewport viewport{0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f};
    VkRect2D scissor{{0, 0}, extent};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // Real, basic procedural sky (task category 3) -- drawn first, depth
    // test/write both disabled (see createSkyPipeline()), so every scene
    // draw below naturally overdraws whatever it actually covers and the
    // sky gradient only ever shows through where nothing else was drawn.
    // Same fullscreen-triangle draw shape drawBloomAndComposite() already
    // uses (vkCmdDraw(3,...), no vertex/index buffer bound).
    // Kronos ("Avatar Preview Rendering" pre-launch fix): skipped
    // entirely for useFlatBackground scenes -- see colorAttachment's own
    // clearValue comment just above. The clear color alone is the whole
    // background for these scenes now.
    if (!useFlatBackground) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, skyPipeline_);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, skyPipelineLayout_, 0, 1,
                                 &frame.sceneDescriptorSet, 0, nullptr);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        recordDraw(3, 1);
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, scenePipeline_);
    applySceneShadingRate(cmd);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, scenePipelineLayout_, 0, 1,
                             &frame.sceneDescriptorSet, 0, nullptr);
    // set=2 (the bindless texture array) -- bound ONCE per pass, not per
    // entity. That is the entire point of the migration: the per-draw
    // vkCmdBindDescriptorSets for set=1 in the loop below disappears.
    if (bindlessInitialised_ && bindlessSet_ != VK_NULL_HANDLE) {
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, scenePipelineLayout_, 2, 1, &bindlessSet_, 0,
                                 nullptr);
    }

    // TODO(frame graph, §4.1): everything below this line is the opaque
    // (Forward+) pass -- see this method's declaration in Renderer.hpp for
    // where shadows/RT/transparency/post attach around it.
    VkPipeline boundScenePipeline = scenePipeline_;
    auto view = ecs.view<Transform, Renderable>();
    SceneSpatialIndex* culling = nullptr;
    if (frustumCulling_) {
        culling = &ecs.raw().ctx().emplace<SceneSpatialIndex>();
        culling->sync(ecs, meshLibrary);
        visibleEntities_.clear();
        culling->cull(ubo.viewProjNoJitter, visibleEntities_);
        frameObjectsVisible_ += static_cast<uint32_t>(culling->stats().visible);
        frameObjectsCulled_ += static_cast<uint32_t>(culling->stats().culled);
    }
    activeCulling_ = culling;
    auto forEachDrawn = [&](auto&& draw) {
        if (culling != nullptr) {
            for (EntityId entity : visibleEntities_) draw(entity);
        } else {
            for (auto entity : view) draw(entity);
        }
    };
    auto worldMatrixOf = [&](EntityId entity) {
        if (culling != nullptr) {
            if (const glm::mat4* world = culling->worldMatrix(entity)) return *world;
        }
        return hierarchy::computeWorldMatrix(ecs, entity);
    };
    auto drawOpaque = [&](const Renderable& renderable, EntityId recordKey, const glm::mat4& model) {
        // instanced entities are drawn by drawInstancedBatches() below,
        // via a batched vkCmdDrawIndexed(instanceCount=N) instead of one
        // individual draw + push-constant update each -- see
        // Components.hpp's Renderable::instanced. Glass/water entities
        // (transmission > 0) are drawn by the dedicated glass loop right
        // below instead of this opaque one -- see Renderable::transmission's
        // own comment and shaders/glass.frag's header comment.
        const Mesh* mesh = meshLibrary.get(renderable.meshHandle);
        if (!mesh) return;

        VkPipeline wantedPipeline = shaderCostViewActive() ? shaderCostPipeline_ : scenePipeline_;
        if (renderable.surfacePipeline != Renderable::kInvalidHandle) {
            auto surface = surfacePipelines_.find(renderable.surfacePipeline);
            if (surface != surfacePipelines_.end()) wantedPipeline = surface->second;
        }
        if (wantedPipeline != boundScenePipeline) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, wantedPipeline);
            applySceneShadingRate(cmd);
            boundScenePipeline = wantedPipeline;
        }

        ObjectPushConstants push{};
        push.model = model;
        push.baseColor = renderable.baseColor;
        push.metallicRoughness = glm::vec4(renderable.metallic, renderable.roughness, renderable.normalIntensity,
                                            renderable.useTriplanarProjection ? 1.0f : 0.0f);
        push.emissive = glm::vec4(renderable.emissiveColor, renderable.emissiveIntensity);
        push.textureIndices = packTextureIndices(renderable, textureLibrary);
        // Set after packTextureIndices(), which returns an all-zero uvec4
        // whenever bindless isn't initialised -- see SceneTypes.hpp's own
        // comment on why this flag can't ride inside that helper.
        push.textureIndices.w = renderable.unlitSilhouette ? 1u : 0u;
        push.textureIndices.z |= pushObjectRecord(frame, recordKey, renderable.layers, push.model) << 16;
        vkCmdPushConstants(cmd, scenePipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                            0, sizeof(push), &push);

        if (!bindlessInitialised_) {
            // set=1 (material textures) -- bound per-entity since it varies
            // per-entity, unlike set=0 (bound once above the loop). A
            // VK_NULL_HANDLE result (pool exhausted) just skips the rebind,
            // leaving whatever set=1 was last bound -- see this function's
            // doc comment on why that degrade is acceptable.
            //
            // The bindless variant declares no set=1 at all (the whole
            // point: one descriptor set bound once per pass instead of a
            // rebind per entity), so this per-draw work is skipped entirely
            // rather than allocating sets nothing will read.
            VkDescriptorSet materialSet = getOrCreateMaterialDescriptorSet(renderable, textureLibrary);
            if (materialSet != VK_NULL_HANDLE) {
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, scenePipelineLayout_, 1, 1, &materialSet,
                                         0, nullptr);
            }
        }

        VkDeviceSize offset = 0;
        VkBuffer vb = mesh->vertexBuffer();
        vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &offset);
        vkCmdBindIndexBuffer(cmd, mesh->indexBuffer(), 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, mesh->indexCount(), 1, 0, 0, 0);
        recordDraw(mesh->indexCount(), 1);
    };
    for (auto& [meshHandle, entities] : automaticInstances_) entities.clear();
    auto instanceable = [&](const Renderable& renderable) {
        if (!automaticInstancing_ || shaderCostViewActive() || renderable.surfacePipeline != Renderable::kInvalidHandle) {
            return false;
        }
        // Without bindless, instances can't carry their own textures.
        return bindlessInitialised_ ||
               (renderable.albedoTexture == Renderable::kInvalidHandle &&
                renderable.normalTexture == Renderable::kInvalidHandle &&
                renderable.metallicTexture == Renderable::kInvalidHandle &&
                renderable.roughnessTexture == Renderable::kInvalidHandle &&
                renderable.aoTexture == Renderable::kInvalidHandle);
    };
    forEachDrawn([&](EntityId entity) {
        const auto& renderable = view.get<Renderable>(entity);
        if (!renderable.visible || renderable.instanced || renderable.transmission > 0.0f) return;
        if (activeBatching_ != nullptr && activeBatching_->isBatched(entity)) return;
        if (instanceable(renderable)) {
            automaticInstances_[renderable.meshHandle].push_back(entity);
            return;
        }
        drawOpaque(renderable, entity, worldMatrixOf(entity));
    });
    for (auto it = automaticInstances_.begin(); it != automaticInstances_.end();) {
        auto& entities = it->second;
        if (entities.empty()) {
            it = automaticInstances_.erase(it);
            continue;
        }
        if (entities.size() < kMinAutomaticInstances) {
            for (EntityId entity : entities) drawOpaque(view.get<Renderable>(entity), entity, worldMatrixOf(entity));
            entities.clear();
        }
        ++it;
    }
    if (activeBatching_ != nullptr) {
        const Frustum frustum = Frustum::fromViewProjection(ubo.viewProjNoJitter);
        const auto& batches = activeBatching_->batches();
        for (size_t i = 0; i < batches.size(); ++i) {
            const StaticBatchSet::Batch& batch = batches[i];
            if (!batch.active) continue;
            ++frameStaticBatches_;
            frameStaticBatchedObjects_ += static_cast<uint32_t>(batch.members.size());
            if (frustumCulling_ && !frustum.intersects(batch.bounds)) continue;
            // Version 0xFFF never belongs to a live entity, so these keys can't collide with one.
            const auto recordKey = static_cast<EntityId>(0xFFF00000u | static_cast<uint32_t>(i));
            drawOpaque(batch.material, recordKey, glm::mat4(1.0f));
        }
    }

    // Kronos ("Real-Time Rendering Evolved" trailer): real glass/water --
    // a second, small pass over the same entity view, routed to
    // glassPipeline_ instead of scenePipeline_ (see shaders/glass.frag's
    // own header comment). Still inside this same vkCmdBeginRendering
    // scope, still depth-testing/writing against the same depth
    // attachment the opaque loop above just wrote, so glass correctly
    // sits behind/in front of opaque geometry -- only its own shading
    // technique differs, not its place in the depth-sorted world.
    if (glassPipeline_ != VK_NULL_HANDLE) {
        bool boundGlassPipeline = false;
        forEachDrawn([&](EntityId entity) {
            auto& renderable = view.get<Renderable>(entity);
            if (!renderable.visible || renderable.transmission <= 0.0f) return;
            const Mesh* mesh = meshLibrary.get(renderable.meshHandle);
            if (!mesh) return;

            if (!boundGlassPipeline) {
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, glassPipeline_);
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, glassPipelineLayout_, 0, 1,
                                         &frame.sceneDescriptorSet, 0, nullptr);
                boundGlassPipeline = true;
            }

            GlassPushConstants push{};
            push.model = worldMatrixOf(entity);
            push.tintColor = glm::vec4(glm::vec3(renderable.baseColor), renderable.transmission);
            push.params = glm::vec4(renderable.transmissionIor, renderable.roughness, 0.0f, 0.0f);
            vkCmdPushConstants(cmd, glassPipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                sizeof(push), &push);

            VkDeviceSize offset = 0;
            VkBuffer vb = mesh->vertexBuffer();
            vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &offset);
            vkCmdBindIndexBuffer(cmd, mesh->indexBuffer(), 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cmd, mesh->indexCount(), 1, 0, 0, 0);
            recordDraw(mesh->indexCount(), 1);
        });
    }

    // Instanced draws don't get per-instance textures (see Components.hpp's
    // Renderable texture-field comment) -- but scene_instanced.vert also
    // feeds the shared scene.frag, which now declares set=1 sampler
    // bindings, so *something* valid must be bound at set=1 for these
    // draws too, or sampling an unbound descriptor is undefined behavior.
    // Binding the default (all-fallback) material set once, before the
    // whole batch, keeps every instanced draw's set=1 valid without
    // needing per-instance-correct textures.
    Renderable defaultMaterial{};
    VkDescriptorSet defaultMaterialSet = getOrCreateMaterialDescriptorSet(defaultMaterial, textureLibrary);
    if (defaultMaterialSet != VK_NULL_HANDLE) {
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, scenePipelineLayout_, 1, 1, &defaultMaterialSet,
                                 0, nullptr);
    }
    drawInstancedBatches(cmd, frame, ecs, meshLibrary, textureLibrary, automaticInstances_, [&](EntityId entity) {
        drawOpaque(view.get<Renderable>(entity), entity, worldMatrixOf(entity));
    });
    activeCulling_ = nullptr;
    activeBatching_ = nullptr;
    // Its own pipeline (not scenePipelineLayout_-based), so it rebinds
    // pipeline/set=0 itself the first time it actually has something to
    // draw -- see its own doc comment. A no-op (no rebind at all) when
    // `riggedMeshLibrary` is null or nothing in `ecs` has a
    // SkinnedRenderable.
    drawSkinnedEntities(cmd, frame, ecs, riggedMeshLibrary, textureLibrary);

    vkCmdEndRendering(cmd);

    // Kronos: soft particles run in their own pass, after everything that
    // writes depth.
    //
    // They SAMPLE the depth buffer while also depth-testing against it.
    // Doing that inside the opaque pass meant the image was in
    // DEPTH_ATTACHMENT_OPTIMAL while a descriptor declared it shader-
    // readable -- VUID-vkCmdDrawIndexed-imageLayout-00344, ten times a
    // frame. DEPTH_READ_ONLY_OPTIMAL is the layout that legitimately
    // permits both at once, and it is only valid because particles
    // already disable depth writes.
    //
    // This also moves particles after skinned meshes, which is a
    // correctness improvement in its own right: blended effects belong
    // last, so solid geometry occludes them properly.
    // LOAD, not CLEAR: this pass composites onto what was just drawn.
    std::array<VkRenderingAttachmentInfo, 2> particleColorAttachments = sceneColorAttachments;
    for (VkRenderingAttachmentInfo& attachment : particleColorAttachments) attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    for (VkImage image : {frame.hdrImage, frame.velocity.image}) {
        render::imageBarrier(cmd, image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                             VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                             VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    }
    if (depthImage != VK_NULL_HANDLE) {
        transitionImage(cmd, depthImage, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                         VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL,
                         VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                         VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                         VK_IMAGE_ASPECT_DEPTH_BIT, 1);

        VkRenderingAttachmentInfo particleDepth = depthAttachment;
        particleDepth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;
        particleDepth.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        particleDepth.storeOp = VK_ATTACHMENT_STORE_OP_NONE;

        VkRenderingInfo particlePass{VK_STRUCTURE_TYPE_RENDERING_INFO};
        particlePass.renderArea = {{0, 0}, extent};
        particlePass.layerCount = 1;
        particlePass.colorAttachmentCount = static_cast<uint32_t>(particleColorAttachments.size());
        particlePass.pColorAttachments = particleColorAttachments.data();
        particlePass.pDepthAttachment = &particleDepth;

        vkCmdBeginRendering(cmd, &particlePass);
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &scissor);
        drawParticles(cmd, frame, particleSystem);
        vkCmdEndRendering(cmd);

        // Back to attachment layout so the next frame's clear/write is
        // valid, and so any later pass sees the layout it expects.
        transitionImage(cmd, depthImage, VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL,
                         VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                         VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT, VK_IMAGE_ASPECT_DEPTH_BIT, 1);
    } else {
        // No depth image handle available (an auxiliary scene path):
        // keep the previous behaviour rather than skipping particles.
        VkRenderingInfo particleOnly = renderingInfo;
        particleOnly.pColorAttachments = particleColorAttachments.data();
        vkCmdBeginRendering(cmd, &particleOnly);
        drawParticles(cmd, frame, particleSystem);
        vkCmdEndRendering(cmd);
    }

    const glm::mat4 currentViewProj = ubo.viewProjNoJitter;

    constexpr VkPipelineStageFlags2 kPostReadStages =
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    transitionImage(cmd, frame.hdrImage, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                     VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_2_SHADER_READ_BIT,
                     VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, kPostReadStages);
    transitionImage(cmd, depthImage, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                     VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_2_SHADER_READ_BIT,
                     VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, kPostReadStages,
                     VK_IMAGE_ASPECT_DEPTH_BIT);
    render::imageBarrier(cmd, frame.velocity.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, kPostReadStages, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

    // TAA runs before SSR/fog/cinematic so every later pass sees the stable, anti-aliased image; the
    // resolve writes the new history and copies it back into hdrImage.
    if (taaActive) {
        render::GpuContext ctx = gpuContext();
        if (temporalAA_.ensureHistory(ctx, history, extent) &&
            temporalAA_.bind(ctx, frame.taaBinding, history, frame.hdrView, frame.velocity.view, depthView)) {
            temporalAA_.resolve(cmd, frame.taaBinding, history, frame.hdrImage, taaFeedback_, motionVectorDebugView_);
        }
    } else {
        history.historyValid = false;
    }
    history.previousJitter = jitter;
    history.previousViewProjNoJitter = currentViewProj;
    history.hasPreviousViewProj = true;
    history.advanceObjects();
    ++history.frameIndex;

    // Kronos ("Rendering Fidelity" -- SSR fallback pass): real
    // screen-space reflections run *first* in the post-FX chain, before
    // volumetric fog -- reads frame.hdrImage (just transitioned above) +
    // depth, writes frame.ssrImage. Reflections should themselves be
    // hazed by volumetric fog (real atmosphere sits between the camera
    // and everything, including a reflective surface), not the other way
    // around -- see ensureVolumetricFogTargets()'s own comment for how
    // its input becomes conditional on this once SSR is enabled.
    drawSSRPass(cmd, frame, extent);

    // Kronos ("Rendering Fidelity Foundation" Phase 1.2): real volumetric
    // fog runs next in the post-FX chain -- reads frame.hdrImage/frame.ssrImage
    // (just transitioned/written above) + depth, writes frame.fogImage. See
    // ensureCinematicTarget()'s own comment for why this has to happen
    // before drawCinematicPass(): Cinematic Mode's own input is
    // conditional on fog's output once fog is enabled.
    drawVolumetricFogPass(cmd, frame, extent);

    drawCinematicPass(cmd, frame, extent);

    // Sprint 16 god rays: the sun's own real screen-space position this
    // frame, projected from a point far along the *reverse* of the light's
    // travel direction (lightDirectionWS is where the light travels *to*,
    // so the light source itself sits back along -directionWS) -- the
    // standard technique for giving a directional light a real screen
    // position to scatter a radial light-shaft effect from. `w > 0`
    // (rather than checking the projected NDC bounds) is what
    // "in front of the camera" actually means after a perspective divide;
    // composite.frag's own clamp handles a sun that's in front of the
    // camera but outside the visible screen rect.
    glm::vec3 towardSun = -glm::normalize(lighting_.directionWS);
    glm::vec4 sunClip = currentViewProj * glm::vec4(camera.position + towardSun * 1000.0f, 1.0f);
    bool sunVisible = sunClip.w > 0.0f;
    glm::vec2 sunScreenUV = sunVisible ? (glm::vec2(sunClip.x, sunClip.y) / sunClip.w) * 0.5f + 0.5f : glm::vec2(0.5f);

    // Bloom + exposure + tonemap: reads frame.hdrImage (or, with
    // Cinematic Mode on, frame.cinematicImage -- see
    // ensureCinematicTarget()'s source-repoint logic) back, writes the
    // caller's actual colorImage/colorView as the final step -- see that
    // method's comment for the pass structure.
    drawBloomAndComposite(cmd, frame, colorImage, colorView, extent, sunScreenUV, sunVisible, applyBloom);
}

void Renderer::drawSceneInto(VkCommandBuffer cmd, VkImage colorImage, VkImageView colorView, VkImage depthImage,
                              VkImageView depthView, VkExtent2D extent, const Camera& camera, ECS& ecs,
                              MeshLibrary& meshLibrary, const ParticleSystem& particleSystem,
                              TextureLibrary& textureLibrary, RiggedMeshLibrary* riggedMeshLibrary) {
    drawSceneIntoImpl(frames_[currentFrame_], cmd, colorImage, colorView, depthImage, depthView, extent, camera, ecs,
                       meshLibrary, particleSystem, textureLibrary, riggedMeshLibrary);
}

void Renderer::drawSceneInto(AuxiliarySceneHandle handle, VkCommandBuffer cmd, VkImage colorImage,
                              VkImageView colorView, VkImage depthImage, VkImageView depthView, VkExtent2D extent,
                              const Camera& camera, ECS& ecs, MeshLibrary& meshLibrary,
                              const ParticleSystem& particleSystem, TextureLibrary& textureLibrary,
                              RiggedMeshLibrary* riggedMeshLibrary, bool applyWeatherEffects, bool applyBloom,
                              bool suppressSunDisk, bool useFlatBackground) {
    if (handle == kInvalidAuxiliaryScene || handle >= auxiliaryScenes_.size()) {
        std::fprintf(stderr, "Renderer: drawSceneInto() called with an invalid AuxiliarySceneHandle.\n");
        return;
    }
    // Kronos ("Avatar Scene Lighting Calibration Pass" / "Avatar Preview
    // Rendering" pre-launch fix): the four flags above default (see this
    // function's own .hpp declaration) to exactly what used to be
    // hardcoded here -- no weather, no bloom, a suppressed sun disk, a
    // flat background -- so studio::ThumbnailCameraRig/studio::PreviewScene
    // (every existing caller) behave byte-identically. Kronos ("Cinematic
    // Camera Physics & Post-Processing Pipeline"): trailer::CaptureRig
    // now passes real values instead, so an exported cinematic frame
    // gets the same real bloom/weather/sky/background the live Studio
    // viewport's own plain drawSceneInto() overload already renders --
    // see that overload's own real (true/true/false/false) defaults.
    drawSceneIntoImpl(auxiliaryScenes_[handle], cmd, colorImage, colorView, depthImage, depthView, extent, camera, ecs,
                       meshLibrary, particleSystem, textureLibrary, riggedMeshLibrary, applyWeatherEffects, applyBloom,
                       suppressSunDisk, useFlatBackground);
}

} // namespace engine::core
