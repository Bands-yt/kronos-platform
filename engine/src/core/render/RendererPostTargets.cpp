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

bool Renderer::ensurePostProcessTargets(FrameSync& frame, VkExtent2D extent, VkImageView depthView) {
    bool sizeUnchanged = frame.hdrImage != VK_NULL_HANDLE && frame.postProcessExtent.width == extent.width &&
                          frame.postProcessExtent.height == extent.height;
    if (sizeUnchanged) {
        // Sprint 16: the hdrImage/bloomImage side is already correctly
        // sized -- only the (much cheaper) cinematic-target lazy-alloc and
        // source-repoint checks below might still need to run, so this
        // deliberately doesn't early-return the way the pre-Sprint-16
        // version of this function did. Kronos Phase 1.2: fog must be
        // ensured *before* cinematic -- see ensureVolumetricFogTargets()'s
        // own comment. Kronos ("Rendering Fidelity" -- SSR): SSR must be
        // ensured before fog -- see ensureSSRTargets()'s own comment.
        if (!ensureSSRTargets(frame, extent, depthView)) return false;
        if (!ensureVolumetricFogTargets(frame, extent, depthView)) return false;
        if (!ensureCinematicTarget(frame, extent, depthView)) return false;
        if (!ensureLuminanceTarget(frame)) return false;
        if (!ensureParticleDepthDescriptor(frame, depthView)) return false;
        return true;
    }

    destroyPostProcessTargets(frame);
    frame.postProcessExtent = extent;

    VkExtent2D bloomExtent{std::max(1u, static_cast<uint32_t>(static_cast<float>(extent.width) * kBloomDownsampleFactor)),
                            std::max(1u, static_cast<uint32_t>(static_cast<float>(extent.height) * kBloomDownsampleFactor))};

    auto createTarget = [&](VkExtent2D targetExtent, VkImage& outImage, VmaAllocation& outAlloc,
                             VkImageView& outView) -> bool {
        VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent = {targetExtent.width, targetExtent.height, 1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = kHDRFormat;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        // TRANSFER_DST: the TAA resolve copies its output back into the HDR target.
        imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

        if (vmaCreateImage(allocator_, &imageInfo, &allocInfo, &outImage, &outAlloc, nullptr) != VK_SUCCESS) {
            std::fprintf(stderr, "Renderer: vmaCreateImage (post-process target) failed.\n");
            return false;
        }

        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image = outImage;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = kHDRFormat;
        viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        if (vkCreateImageView(device_, &viewInfo, nullptr, &outView) != VK_SUCCESS) {
            std::fprintf(stderr, "Renderer: vkCreateImageView (post-process target) failed.\n");
            return false;
        }
        return true;
    };

    if (!createTarget(extent, frame.hdrImage, frame.hdrAllocation, frame.hdrView)) return false;
    if (!createTarget(bloomExtent, frame.bloomImage, frame.bloomAllocation, frame.bloomView)) return false;
    if (!render::createImage(gpuContext(), kVelocityFormat, extent.width, extent.height, 1, 1, false,
                             VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, frame.velocity)) {
        std::fprintf(stderr, "Renderer: velocity target creation failed.\n");
        return false;
    }
    if (!motionBlur_.bind(gpuContext(), frame.motionBlurBinding, extent, frame.velocity.view)) {
        std::fprintf(stderr, "Renderer: motion blur target creation failed.\n");
        return false;
    }

    VkDescriptorSetAllocateInfo singleAllocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    singleAllocInfo.descriptorPool = postProcessDescriptorPool_;
    singleAllocInfo.descriptorSetCount = 1;
    singleAllocInfo.pSetLayouts = &postProcessSingleSetLayout_;
    if (vkAllocateDescriptorSets(device_, &singleAllocInfo, &frame.bloomExtractDescriptorSet) != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vkAllocateDescriptorSets (bloom extract) failed.\n");
        return false;
    }

    VkDescriptorSetAllocateInfo dualAllocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dualAllocInfo.descriptorPool = postProcessDescriptorPool_;
    dualAllocInfo.descriptorSetCount = 1;
    dualAllocInfo.pSetLayouts = &postProcessCompositeSetLayout_;
    if (vkAllocateDescriptorSets(device_, &dualAllocInfo, &frame.compositeDescriptorSet) != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vkAllocateDescriptorSets (composite) failed.\n");
        return false;
    }

    // Valid to reference views here before they're ever transitioned to
    // SHADER_READ_ONLY_OPTIMAL for the first time -- same "the write only
    // records intent" reasoning as the shadow sampler descriptor write in
    // createSceneDescriptorResources().
    VkDescriptorImageInfo hdrImageInfo{postProcessSampler_, frame.hdrView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorImageInfo bloomImageInfo{postProcessSampler_, frame.bloomView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    // Kronos ("Cinematic Camera Physics & Post-Processing Pipeline" --
    // real 3D LUT support): lutImageView_ is always real and valid by
    // the time any frame slot first reaches here -- initialize() seeds a
    // real identity LUT before any FrameSync/AuxiliarySceneHandle can
    // allocate a compositeDescriptorSet at all (see that call site's own
    // comment).
    VkDescriptorImageInfo lutImageInfo{postProcessSampler_, lutImageView_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

    std::array<VkWriteDescriptorSet, 4> writes{};
    writes[0] = VkWriteDescriptorSet{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    writes[0].dstSet = frame.bloomExtractDescriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].pImageInfo = &hdrImageInfo;

    writes[1] = VkWriteDescriptorSet{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    writes[1].dstSet = frame.compositeDescriptorSet;
    writes[1].dstBinding = 0;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].pImageInfo = &hdrImageInfo;

    writes[2] = VkWriteDescriptorSet{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    writes[2].dstSet = frame.compositeDescriptorSet;
    writes[2].dstBinding = 1;
    writes[2].descriptorCount = 1;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[2].pImageInfo = &bloomImageInfo;

    writes[3] = VkWriteDescriptorSet{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    writes[3].dstSet = frame.compositeDescriptorSet;
    writes[3].dstBinding = 2;
    writes[3].descriptorCount = 1;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[3].pImageInfo = &lutImageInfo;

    vkUpdateDescriptorSets(device_, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    // hdrView is correct after a fresh (re)allocation above -- see the
    // callers below. Kronos Phase 1.2: fog must be ensured before
    // cinematic here too, same reasoning as the "size unchanged" path
    // above. Kronos ("Rendering Fidelity" -- SSR): SSR must be ensured
    // before fog, same reasoning.
    frame.cinematicSourceBound = false;
    frame.fogSourceBoundForComposite = false;
    frame.ssrSourceBoundForComposite = false;
    if (!ensureSSRTargets(frame, extent, depthView)) return false;
    if (!ensureVolumetricFogTargets(frame, extent, depthView)) return false;
    if (!ensureCinematicTarget(frame, extent, depthView)) return false;
    if (!ensureLuminanceTarget(frame)) return false;
    if (!ensureParticleDepthDescriptor(frame, depthView)) return false;
    return true;
}

// Kronos ("Rendering Fidelity" -- SSR fallback pass): lazily allocates
// frame.ssrImage/ssrView/ssrInputDescriptorSet the first time SSR is
// actually enabled for this frame slot (never eagerly -- same convention
// as ensureVolumetricFogTargets() below), and keeps
// frame.ssrInputDescriptorSet's own bindings current. This descriptor
// set's binding 0 is *never* conditional -- it always points at
// frame.hdrView, since SSR (like fog) can never legally read its own
// output. Must run before ensureVolumetricFogTargets() -- see this
// function's own declaration comment in Renderer.hpp.
bool Renderer::ensureSSRTargets(FrameSync& frame, VkExtent2D extent, VkImageView depthView) {
    if (ssrEnabled_ && frame.ssrImage == VK_NULL_HANDLE) {
        VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent = {extent.width, extent.height, 1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = kHDRFormat;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

        if (vmaCreateImage(allocator_, &imageInfo, &allocInfo, &frame.ssrImage, &frame.ssrAllocation, nullptr) !=
            VK_SUCCESS) {
            std::fprintf(stderr, "Renderer: vmaCreateImage (SSR target) failed.\n");
            return false;
        }

        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image = frame.ssrImage;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = kHDRFormat;
        viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        if (vkCreateImageView(device_, &viewInfo, nullptr, &frame.ssrView) != VK_SUCCESS) {
            std::fprintf(stderr, "Renderer: vkCreateImageView (SSR target) failed.\n");
            return false;
        }

        VkDescriptorSetAllocateInfo allocSetInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocSetInfo.descriptorPool = postProcessDescriptorPool_;
        allocSetInfo.descriptorSetCount = 1;
        allocSetInfo.pSetLayouts = &cinematicDescriptorSetLayout_; // reused shape -- see this field's own Renderer.hpp comment
        if (vkAllocateDescriptorSets(device_, &allocSetInfo, &frame.ssrInputDescriptorSet) != VK_SUCCESS) {
            std::fprintf(stderr, "Renderer: vkAllocateDescriptorSets (SSR input) failed.\n");
            return false;
        }
        frame.ssrInputViews = {};
    }

    if (frame.ssrInputDescriptorSet != VK_NULL_HANDLE) {
        // Always hdrView/depthView, never conditional (see this function's
        // header comment); rewritten only when either view changes.
        VkDescriptorImageInfo hdrInfo{postProcessSampler_, frame.hdrView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkDescriptorImageInfo depthInfo{depthSampler_, depthView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        const std::array<VkImageView, 2> views{frame.hdrView, depthView};
        if (views != frame.ssrInputViews) {
            frame.ssrInputViews = views;
            std::array<VkWriteDescriptorSet, 2> writes{};
            writes[0] = VkWriteDescriptorSet{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            writes[0].dstSet = frame.ssrInputDescriptorSet;
            writes[0].dstBinding = 0;
            writes[0].descriptorCount = 1;
            writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[0].pImageInfo = &hdrInfo;
            writes[1] = VkWriteDescriptorSet{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            writes[1].dstSet = frame.ssrInputDescriptorSet;
            writes[1].dstBinding = 1;
            writes[1].descriptorCount = 1;
            writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[1].pImageInfo = &depthInfo;
            vkUpdateDescriptorSets(device_, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
        }
    }
    return true;
}

// Kronos ("Rendering Fidelity Foundation" Phase 1.2): lazily allocates
// frame.fogImage/fogView/fogInputDescriptorSet the first time volumetric
// fog is actually enabled for this frame slot (never eagerly -- same
// convention as ensureCinematicTarget() below), and keeps
// frame.fogInputDescriptorSet's own bindings current. Unlike
// ensureCinematicTarget(), this descriptor set's binding 0 is *never*
// conditional -- it always points at frame.hdrView, since the fog pass
// can never legally read its own output (see FrameSync::fogInputDescriptorSet's
// own comment). Must run before ensureCinematicTarget() -- see this
// function's own declaration comment in Renderer.hpp. Kronos ("Rendering
// Fidelity" -- SSR): unlike its own binding 0, this function's binding 0
// (below) *is* conditional -- frame.ssrView if SSR ran, else
// frame.hdrView -- so volumetric fog picks up SSR's reflections rather
// than bypassing them (fog should haze reflections too, matching real
// atmosphere-over-everything behavior).
bool Renderer::ensureVolumetricFogTargets(FrameSync& frame, VkExtent2D extent, VkImageView depthView) {
    if (volumetricFogEnabled_ && frame.fogImage == VK_NULL_HANDLE) {
        VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent = {extent.width, extent.height, 1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = kHDRFormat;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

        if (vmaCreateImage(allocator_, &imageInfo, &allocInfo, &frame.fogImage, &frame.fogAllocation, nullptr) !=
            VK_SUCCESS) {
            std::fprintf(stderr, "Renderer: vmaCreateImage (volumetric fog target) failed.\n");
            return false;
        }

        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image = frame.fogImage;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = kHDRFormat;
        viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        if (vkCreateImageView(device_, &viewInfo, nullptr, &frame.fogView) != VK_SUCCESS) {
            std::fprintf(stderr, "Renderer: vkCreateImageView (volumetric fog target) failed.\n");
            return false;
        }

        VkDescriptorSetAllocateInfo allocSetInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocSetInfo.descriptorPool = postProcessDescriptorPool_;
        allocSetInfo.descriptorSetCount = 1;
        allocSetInfo.pSetLayouts = &cinematicDescriptorSetLayout_; // reused shape -- see this field's own Renderer.hpp comment
        if (vkAllocateDescriptorSets(device_, &allocSetInfo, &frame.fogInputDescriptorSet) != VK_SUCCESS) {
            std::fprintf(stderr, "Renderer: vkAllocateDescriptorSets (volumetric fog input) failed.\n");
            return false;
        }
        frame.fogInputViews = {};
    }

    if (frame.fogInputDescriptorSet != VK_NULL_HANDLE) {
        // Binding 0 is conditional on SSR (see this function's header
        // comment); rewritten only when either view changes.
        VkImageView fogInput = (ssrEnabled_ && frame.ssrView != VK_NULL_HANDLE) ? frame.ssrView : frame.hdrView;
        VkDescriptorImageInfo hdrInfo{postProcessSampler_, fogInput, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkDescriptorImageInfo depthInfo{depthSampler_, depthView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        const std::array<VkImageView, 2> views{fogInput, depthView};
        if (views != frame.fogInputViews) {
            frame.fogInputViews = views;
            std::array<VkWriteDescriptorSet, 2> writes{};
            writes[0] = VkWriteDescriptorSet{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            writes[0].dstSet = frame.fogInputDescriptorSet;
            writes[0].dstBinding = 0;
            writes[0].descriptorCount = 1;
            writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[0].pImageInfo = &hdrInfo;
            writes[1] = VkWriteDescriptorSet{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            writes[1].dstSet = frame.fogInputDescriptorSet;
            writes[1].dstBinding = 1;
            writes[1].descriptorCount = 1;
            writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[1].pImageInfo = &depthInfo;
            vkUpdateDescriptorSets(device_, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
        }
    }
    return true;
}

// Sprint 16: lazily allocates frame.cinematicImage/cinematicView/
// cinematicDescriptorSet the first time Cinematic Mode is actually
// enabled for this frame slot (never eagerly -- most FrameSync slots,
// e.g. every Studio preview scene, never turn it on), and repoints
// bloomExtractDescriptorSet's/compositeDescriptorSet's binding 0 between
// hdrView and cinematicView whenever isCinematicModeEnabled() has
// changed since the last time this ran for `frame`. Once allocated, the
// cinematic target is kept (not freed) if the mode is later disabled --
// cheap to keep, avoids alloc/free churn on every toggle. Kronos Phase 1.2:
// this pass's own *input* (binding 0 of frame.cinematicDescriptorSet) is
// now also conditional -- frame.fogView if volumetric fog is on, else
// frame.hdrView -- so Cinematic Mode picks up fog's output rather than
// bypassing it, and the *final* bloom/composite source below is a real
// 3-way choice (hdr/fog/cinematic) instead of a plain boolean.
bool Renderer::ensureCinematicTarget(FrameSync& frame, VkExtent2D extent, VkImageView depthView) {
    if (cinematicModeEnabled_ && frame.cinematicImage == VK_NULL_HANDLE) {
        VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent = {extent.width, extent.height, 1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = kHDRFormat;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

        if (vmaCreateImage(allocator_, &imageInfo, &allocInfo, &frame.cinematicImage, &frame.cinematicAllocation,
                            nullptr) != VK_SUCCESS) {
            std::fprintf(stderr, "Renderer: vmaCreateImage (cinematic target) failed.\n");
            return false;
        }

        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image = frame.cinematicImage;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = kHDRFormat;
        viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        if (vkCreateImageView(device_, &viewInfo, nullptr, &frame.cinematicView) != VK_SUCCESS) {
            std::fprintf(stderr, "Renderer: vkCreateImageView (cinematic target) failed.\n");
            return false;
        }

        VkDescriptorSetAllocateInfo allocSetInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocSetInfo.descriptorPool = postProcessDescriptorPool_;
        allocSetInfo.descriptorSetCount = 1;
        allocSetInfo.pSetLayouts = &cinematicDescriptorSetLayout_;
        if (vkAllocateDescriptorSets(device_, &allocSetInfo, &frame.cinematicDescriptorSet) != VK_SUCCESS) {
            std::fprintf(stderr, "Renderer: vkAllocateDescriptorSets (cinematic) failed.\n");
            return false;
        }
        frame.cinematicInputViews = {};
    }

    if (frame.cinematicDescriptorSet != VK_NULL_HANDLE) {
        // depthView can change on a resize even when cinematicImage did not,
        // and binding 0 is fogView when volumetric fog ran, else hdrView;
        // rewritten only when either view changes.
        VkImageView cinematicInput = (volumetricFogEnabled_ && frame.fogView != VK_NULL_HANDLE) ? frame.fogView : frame.hdrView;
        VkDescriptorImageInfo hdrInfo{postProcessSampler_, cinematicInput, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkDescriptorImageInfo depthInfo{depthSampler_, depthView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        const std::array<VkImageView, 2> views{cinematicInput, depthView};
        if (views != frame.cinematicInputViews) {
            frame.cinematicInputViews = views;
            std::array<VkWriteDescriptorSet, 2> writes{};
            writes[0] = VkWriteDescriptorSet{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            writes[0].dstSet = frame.cinematicDescriptorSet;
            writes[0].dstBinding = 0;
            writes[0].descriptorCount = 1;
            writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[0].pImageInfo = &hdrInfo;
            writes[1] = VkWriteDescriptorSet{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            writes[1].dstSet = frame.cinematicDescriptorSet;
            writes[1].dstBinding = 1;
            writes[1].descriptorCount = 1;
            writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[1].pImageInfo = &depthInfo;
            vkUpdateDescriptorSets(device_, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
        }
    }

    // Kronos Phase 1.2 / ("Rendering Fidelity" -- SSR): the real "front of
    // the chain" is now a 4-way choice -- cinematicView (if Cinematic Mode
    // ran; it already consumed fog's own output as *its* input above, so
    // it's always the final choice when it ran at all), else fogView (if
    // fog ran; it already consumed SSR's own output as *its* input, see
    // ensureVolumetricFogTargets()'s own comment), else ssrView (if only
    // SSR ran), else hdrView (none ran). Three independent tracked bools
    // since any one toggle changing independently must trigger this rewrite.
    bool desiredCinematicBound = cinematicModeEnabled_;
    bool desiredFogBound = volumetricFogEnabled_;
    bool desiredSSRBound = ssrEnabled_;
    if ((frame.cinematicSourceBound != desiredCinematicBound || frame.fogSourceBoundForComposite != desiredFogBound ||
         frame.ssrSourceBoundForComposite != desiredSSRBound) &&
        (frame.cinematicImage != VK_NULL_HANDLE || frame.fogImage != VK_NULL_HANDLE || frame.ssrImage != VK_NULL_HANDLE)) {
        // Real validity checks, not just the raw enabled flags -- e.g. if
        // Cinematic Mode is on but its own target failed to allocate this
        // frame (descriptor pool exhaustion, logged there) while fog's
        // target *did* succeed, this must fall through to fogView, not
        // reference a real, but null, cinematicView.
        VkImageView source = (cinematicModeEnabled_ && frame.cinematicImage != VK_NULL_HANDLE) ? frame.cinematicView
                              : (volumetricFogEnabled_ && frame.fogImage != VK_NULL_HANDLE)     ? frame.fogView
                              : (ssrEnabled_ && frame.ssrImage != VK_NULL_HANDLE)                ? frame.ssrView
                                                                                                  : frame.hdrView;
        VkDescriptorImageInfo sourceInfo{postProcessSampler_, source, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        std::array<VkWriteDescriptorSet, 2> writes{};
        writes[0] = VkWriteDescriptorSet{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        writes[0].dstSet = frame.bloomExtractDescriptorSet;
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[0].pImageInfo = &sourceInfo;
        writes[1] = VkWriteDescriptorSet{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        writes[1].dstSet = frame.compositeDescriptorSet;
        writes[1].dstBinding = 0;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].pImageInfo = &sourceInfo;
        vkUpdateDescriptorSets(device_, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
        frame.cinematicSourceBound = desiredCinematicBound;
        frame.fogSourceBoundForComposite = desiredFogBound;
        frame.ssrSourceBoundForComposite = desiredSSRBound;
    }
    return true;
}

// Sprint 16 auto-exposure -- see FrameSync's own comment. A one-time
// allocation (no resize logic): a 1x1 target's size never depends on
// `extent`, unlike ensureCinematicTarget()'s targets.
bool Renderer::ensureLuminanceTarget(FrameSync& frame) {
    if (!cinematicModeEnabled_ || frame.luminanceImage != VK_NULL_HANDLE) return true;

    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = {1, 1, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_R32_SFLOAT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    if (vmaCreateImage(allocator_, &imageInfo, &allocInfo, &frame.luminanceImage, &frame.luminanceAllocation, nullptr) !=
        VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vmaCreateImage (luminance target) failed.\n");
        return false;
    }

    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = frame.luminanceImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R32_SFLOAT;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(device_, &viewInfo, nullptr, &frame.luminanceView) != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vkCreateImageView (luminance target) failed.\n");
        return false;
    }

    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = sizeof(float);
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VmaAllocationCreateInfo bufferAllocInfo{};
    bufferAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    // HOST_ACCESS_RANDOM (not SEQUENTIAL_WRITE): this buffer is *written
    // by the GPU* (vkCmdCopyImageToBuffer) and *read by the CPU* -- the
    // same real readback direction/hint publishing::captureThumbnailToFile()'s
    // own staging buffer already uses, not a CPU-writes-first pattern.
    bufferAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VmaAllocationInfo resultInfo{};
    if (vmaCreateBuffer(allocator_, &bufferInfo, &bufferAllocInfo, &frame.luminanceReadbackBuffer,
                         &frame.luminanceReadbackAllocation, &resultInfo) != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vmaCreateBuffer (luminance readback) failed.\n");
        return false;
    }
    frame.luminanceReadbackMapped = resultInfo.pMappedData;
    *static_cast<float*>(frame.luminanceReadbackMapped) = kAutoExposureTargetLuminance; // real, sane initial value -- not garbage

    VkDescriptorSetAllocateInfo allocSetInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocSetInfo.descriptorPool = postProcessDescriptorPool_;
    allocSetInfo.descriptorSetCount = 1;
    allocSetInfo.pSetLayouts = &postProcessSingleSetLayout_;
    if (vkAllocateDescriptorSets(device_, &allocSetInfo, &frame.luminanceDescriptorSet) != VK_SUCCESS) {
        std::fprintf(stderr, "Renderer: vkAllocateDescriptorSets (luminance) failed.\n");
        return false;
    }
    VkDescriptorImageInfo hdrInfo{postProcessSampler_, frame.hdrView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = frame.luminanceDescriptorSet;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &hdrInfo;
    vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
    return true;
}

// Sprint 16 soft-particle depth fade -- see this method's own .hpp
// comment on why it always runs (not gated on Cinematic Mode).
bool Renderer::ensureParticleDepthDescriptor(FrameSync& frame, VkImageView depthView) {
    if (frame.particleDepthDescriptorSet == VK_NULL_HANDLE) {
        VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocInfo.descriptorPool = postProcessDescriptorPool_;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &postProcessSingleSetLayout_;
        if (vkAllocateDescriptorSets(device_, &allocInfo, &frame.particleDepthDescriptorSet) != VK_SUCCESS) {
            std::fprintf(stderr, "Renderer: vkAllocateDescriptorSets (particle depth) failed.\n");
            return false;
        }
    }
    VkDescriptorImageInfo depthInfo{depthSampler_, depthView, VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = frame.particleDepthDescriptorSet;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &depthInfo;
    vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
    return true;
}

void Renderer::destroyPostProcessTargets(FrameSync& frame) {
    // Real bug (found via validation layers -- vkFreeDescriptorSets/
    // vkDestroyImage "in use by VkCommandBuffer"): this used to free/destroy
    // every post-process descriptor set and image below with no GPU wait at
    // all. ensurePostProcessTargets() calls this synchronously on a live
    // viewport resize, so a previous frame's command buffer could still be
    // executing against frame.hdrView/frame.particleDepthDescriptorSet/etc.
    // when they got torn down -- same class of bug OffscreenTarget::destroy()
    // already had fixed for its own resources, just not for this unrelated
    // per-FrameSync resource set. shutdown() already waits idle once before
    // its own destroyPostProcessTargets() loop, so this is a harmless no-op
    // there; for the resize path and every destroyAuxiliaryScene() caller
    // (PreviewScene/ThumbnailCameraRig/SecondaryViewport/CaptureRig teardown,
    // none of which waited either), this is the actual fix.
    if (frame.particleDepthDescriptorSet != VK_NULL_HANDLE || frame.luminanceDescriptorSet != VK_NULL_HANDLE ||
        frame.cinematicDescriptorSet != VK_NULL_HANDLE || frame.fogInputDescriptorSet != VK_NULL_HANDLE ||
        frame.ssrInputDescriptorSet != VK_NULL_HANDLE || frame.bloomExtractDescriptorSet != VK_NULL_HANDLE ||
        frame.compositeDescriptorSet != VK_NULL_HANDLE || frame.hdrImage != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);
    }

    render::destroyImage(gpuContext(), frame.velocity);
    motionBlur_.releaseBinding(gpuContext(), frame.motionBlurBinding);
    if (frame.particleDepthDescriptorSet != VK_NULL_HANDLE) {
        vkFreeDescriptorSets(device_, postProcessDescriptorPool_, 1, &frame.particleDepthDescriptorSet);
        frame.particleDepthDescriptorSet = VK_NULL_HANDLE;
    }
    if (frame.luminanceDescriptorSet != VK_NULL_HANDLE) {
        vkFreeDescriptorSets(device_, postProcessDescriptorPool_, 1, &frame.luminanceDescriptorSet);
        frame.luminanceDescriptorSet = VK_NULL_HANDLE;
    }
    if (frame.luminanceReadbackBuffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator_, frame.luminanceReadbackBuffer, frame.luminanceReadbackAllocation);
        frame.luminanceReadbackBuffer = VK_NULL_HANDLE;
        frame.luminanceReadbackMapped = nullptr;
    }
    if (frame.luminanceView != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, frame.luminanceView, nullptr);
        frame.luminanceView = VK_NULL_HANDLE;
    }
    if (frame.luminanceImage != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator_, frame.luminanceImage, frame.luminanceAllocation);
        frame.luminanceImage = VK_NULL_HANDLE;
    }
    if (frame.cinematicDescriptorSet != VK_NULL_HANDLE) {
        vkFreeDescriptorSets(device_, postProcessDescriptorPool_, 1, &frame.cinematicDescriptorSet);
        frame.cinematicDescriptorSet = VK_NULL_HANDLE;
    }
    if (frame.cinematicView != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, frame.cinematicView, nullptr);
        frame.cinematicView = VK_NULL_HANDLE;
    }
    if (frame.cinematicImage != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator_, frame.cinematicImage, frame.cinematicAllocation);
        frame.cinematicImage = VK_NULL_HANDLE;
    }
    frame.cinematicSourceBound = false;
    if (frame.fogInputDescriptorSet != VK_NULL_HANDLE) {
        vkFreeDescriptorSets(device_, postProcessDescriptorPool_, 1, &frame.fogInputDescriptorSet);
        frame.fogInputDescriptorSet = VK_NULL_HANDLE;
    }
    if (frame.fogView != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, frame.fogView, nullptr);
        frame.fogView = VK_NULL_HANDLE;
    }
    if (frame.fogImage != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator_, frame.fogImage, frame.fogAllocation);
        frame.fogImage = VK_NULL_HANDLE;
    }
    frame.fogSourceBoundForComposite = false;
    if (frame.ssrInputDescriptorSet != VK_NULL_HANDLE) {
        vkFreeDescriptorSets(device_, postProcessDescriptorPool_, 1, &frame.ssrInputDescriptorSet);
        frame.ssrInputDescriptorSet = VK_NULL_HANDLE;
    }
    if (frame.ssrView != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, frame.ssrView, nullptr);
        frame.ssrView = VK_NULL_HANDLE;
    }
    if (frame.ssrImage != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator_, frame.ssrImage, frame.ssrAllocation);
        frame.ssrImage = VK_NULL_HANDLE;
    }
    frame.ssrSourceBoundForComposite = false;
    if (frame.bloomExtractDescriptorSet != VK_NULL_HANDLE) {
        vkFreeDescriptorSets(device_, postProcessDescriptorPool_, 1, &frame.bloomExtractDescriptorSet);
        frame.bloomExtractDescriptorSet = VK_NULL_HANDLE;
    }
    if (frame.compositeDescriptorSet != VK_NULL_HANDLE) {
        vkFreeDescriptorSets(device_, postProcessDescriptorPool_, 1, &frame.compositeDescriptorSet);
        frame.compositeDescriptorSet = VK_NULL_HANDLE;
    }
    if (frame.hdrView != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, frame.hdrView, nullptr);
        frame.hdrView = VK_NULL_HANDLE;
    }
    if (frame.hdrImage != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator_, frame.hdrImage, frame.hdrAllocation);
        frame.hdrImage = VK_NULL_HANDLE;
    }
    if (frame.bloomView != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, frame.bloomView, nullptr);
        frame.bloomView = VK_NULL_HANDLE;
    }
    if (frame.bloomImage != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator_, frame.bloomImage, frame.bloomAllocation);
        frame.bloomImage = VK_NULL_HANDLE;
    }
    frame.postProcessExtent = {0, 0};
}

} // namespace engine::core
