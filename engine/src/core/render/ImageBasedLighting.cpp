#include "core/render/ImageBasedLighting.hpp"

#include <cmath>

namespace engine::core::render {

namespace {

constexpr VkFormat kIblFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
constexpr uint32_t kShFaceSize = 32;
constexpr uint32_t kPrefilterSamples = 128;
// Clouds drift continuously; re-capture on this cadence while they're on.
constexpr float kCloudRecaptureSeconds = 1.0f;
// Camera-origin changes only matter for atmosphere/cloud parallax.
constexpr float kOriginQuantum = 32.0f;

constexpr VkPipelineStageFlags2 kShadingStages =
    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;

struct CapturePush {
    glm::vec4 zenith, horizon, sunDir, origin, atmosphere, clouds, ground;
};
struct PrefilterPush {
    float perceptualRoughness;
    float sourceSize;
    uint32_t sampleCount;
    float sourceMaxMip;
};
struct ShPush {
    uint32_t faceSize;
    float lod;
};

// Log-space quantisation: ~2% steps regardless of HDR magnitude, so slow
// weather/time-of-day blends trigger an occasional re-capture rather than
// one every frame.
float quantiseRadiance(float v) {
    return std::round(std::log2(std::max(v, 0.0f) + 1e-4f) * 36.0f);
}

void pushRadiance(std::vector<float>& key, glm::vec3 c) {
    for (int i = 0; i < 3; ++i) key.push_back(quantiseRadiance(c[i]));
}

} // namespace

bool ImageBasedLighting::createPass(const GpuContext& ctx, ComputePass& pass, const char* shader,
                                    std::initializer_list<VkDescriptorType> bindings, uint32_t pushBytes) {
    std::vector<VkDescriptorSetLayoutBinding> layoutBindings;
    for (VkDescriptorType type : bindings) {
        VkDescriptorSetLayoutBinding b{};
        b.binding = static_cast<uint32_t>(layoutBindings.size());
        b.descriptorType = type;
        b.descriptorCount = 1;
        b.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        layoutBindings.push_back(b);
    }
    VkDescriptorSetLayoutCreateInfo setInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    setInfo.bindingCount = static_cast<uint32_t>(layoutBindings.size());
    setInfo.pBindings = layoutBindings.data();
    if (vkCreateDescriptorSetLayout(ctx.device, &setInfo, nullptr, &pass.setLayout) != VK_SUCCESS) return false;

    VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT, 0, pushBytes};
    VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &pass.setLayout;
    layoutInfo.pushConstantRangeCount = pushBytes > 0 ? 1 : 0;
    layoutInfo.pPushConstantRanges = &range;
    if (vkCreatePipelineLayout(ctx.device, &layoutInfo, nullptr, &pass.layout) != VK_SUCCESS) return false;

    pass.pipeline = createComputePipeline(ctx, pass.layout, shader);
    return pass.pipeline != VK_NULL_HANDLE;
}

void ImageBasedLighting::destroyPass(const GpuContext& ctx, ComputePass& pass) {
    if (pass.pipeline != VK_NULL_HANDLE) vkDestroyPipeline(ctx.device, pass.pipeline, nullptr);
    if (pass.layout != VK_NULL_HANDLE) vkDestroyPipelineLayout(ctx.device, pass.layout, nullptr);
    if (pass.setLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(ctx.device, pass.setLayout, nullptr);
    pass = {};
}

bool ImageBasedLighting::initialize(const GpuContext& ctx) {
    environmentMips_ = static_cast<uint32_t>(std::log2(static_cast<float>(kEnvironmentSize))) + 1;

    constexpr VkImageUsageFlags kComputeTarget = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    if (!createImage(ctx, kIblFormat, kDfgSize, kDfgSize, 1, 1, false, kComputeTarget, dfg_) ||
        !createImage(ctx, kIblFormat, kEnvironmentSize, kEnvironmentSize, environmentMips_, 6, true,
                     kComputeTarget | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                     environment_) ||
        !createImage(ctx, kIblFormat, kPrefilteredSize, kPrefilteredSize, kPrefilteredMips, 6, true, kComputeTarget,
                     prefiltered_) ||
        !createBuffer(ctx, sizeof(glm::vec4) * 9, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false, sh_)) {
        return false;
    }

    environmentMip0Storage_ = createArrayView(ctx, environment_.image, kIblFormat, 0, 6);
    if (environmentMip0Storage_ == VK_NULL_HANDLE) return false;
    for (uint32_t mip = 0; mip < kPrefilteredMips; ++mip) {
        prefilteredMipStorage_[mip] = createArrayView(ctx, prefiltered_.image, kIblFormat, mip, 6);
        if (prefilteredMipStorage_[mip] == VK_NULL_HANDLE) return false;
    }

    VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
    if (vkCreateSampler(ctx.device, &samplerInfo, nullptr, &sampler_) != VK_SUCCESS) return false;

    constexpr VkDescriptorType kStorageImage = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    constexpr VkDescriptorType kSampled = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    if (!createPass(ctx, dfgPass_, "dfg_lut.comp.spv", {kStorageImage}, 0) ||
        !createPass(ctx, capturePass_, "ibl_capture.comp.spv", {kStorageImage}, sizeof(CapturePush)) ||
        !createPass(ctx, prefilterPass_, "ibl_prefilter.comp.spv", {kSampled, kStorageImage}, sizeof(PrefilterPush)) ||
        !createPass(ctx, shPass_, "ibl_sh.comp.spv", {kSampled, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER}, sizeof(ShPush))) {
        return false;
    }

    std::array<VkDescriptorPoolSize, 3> poolSizes{{
        {kStorageImage, 2 + kPrefilteredMips},
        {kSampled, 1 + kPrefilteredMips},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1},
    }};
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets = 3 + kPrefilteredMips;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    if (vkCreateDescriptorPool(ctx.device, &poolInfo, nullptr, &pool_) != VK_SUCCESS) return false;

    auto allocate = [&](VkDescriptorSetLayout layout, VkDescriptorSet& out) {
        VkDescriptorSetAllocateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        info.descriptorPool = pool_;
        info.descriptorSetCount = 1;
        info.pSetLayouts = &layout;
        return vkAllocateDescriptorSets(ctx.device, &info, &out) == VK_SUCCESS;
    };
    if (!allocate(dfgPass_.setLayout, dfgSet_) || !allocate(capturePass_.setLayout, captureSet_) ||
        !allocate(shPass_.setLayout, shSet_)) {
        return false;
    }
    for (VkDescriptorSet& set : prefilterSets_) {
        if (!allocate(prefilterPass_.setLayout, set)) return false;
    }

    std::vector<VkDescriptorImageInfo> images;
    images.reserve(4 + 2 * kPrefilteredMips);
    std::vector<VkWriteDescriptorSet> writes;
    auto writeImage = [&](VkDescriptorSet set, uint32_t binding, VkDescriptorType type, VkImageView view,
                          VkImageLayout layout) {
        images.push_back({type == kSampled ? sampler_ : VK_NULL_HANDLE, view, layout});
        VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w.dstSet = set;
        w.dstBinding = binding;
        w.descriptorCount = 1;
        w.descriptorType = type;
        w.pImageInfo = &images.back();
        writes.push_back(w);
    };
    writeImage(dfgSet_, 0, kStorageImage, dfg_.view, VK_IMAGE_LAYOUT_GENERAL);
    writeImage(captureSet_, 0, kStorageImage, environmentMip0Storage_, VK_IMAGE_LAYOUT_GENERAL);
    writeImage(shSet_, 0, kSampled, environment_.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    for (uint32_t mip = 0; mip < kPrefilteredMips; ++mip) {
        writeImage(prefilterSets_[mip], 0, kSampled, environment_.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        writeImage(prefilterSets_[mip], 1, kStorageImage, prefilteredMipStorage_[mip], VK_IMAGE_LAYOUT_GENERAL);
    }
    VkDescriptorBufferInfo shInfo{sh_.buffer, 0, VK_WHOLE_SIZE};
    VkWriteDescriptorSet shWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    shWrite.dstSet = shSet_;
    shWrite.dstBinding = 1;
    shWrite.descriptorCount = 1;
    shWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    shWrite.pBufferInfo = &shInfo;
    writes.push_back(shWrite);
    vkUpdateDescriptorSets(ctx.device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    return true;
}

void ImageBasedLighting::shutdown(const GpuContext& ctx) {
    if (pool_ != VK_NULL_HANDLE) vkDestroyDescriptorPool(ctx.device, pool_, nullptr);
    pool_ = VK_NULL_HANDLE;
    destroyPass(ctx, dfgPass_);
    destroyPass(ctx, capturePass_);
    destroyPass(ctx, prefilterPass_);
    destroyPass(ctx, shPass_);
    if (sampler_ != VK_NULL_HANDLE) vkDestroySampler(ctx.device, sampler_, nullptr);
    sampler_ = VK_NULL_HANDLE;
    for (VkImageView& view : prefilteredMipStorage_) {
        if (view != VK_NULL_HANDLE) vkDestroyImageView(ctx.device, view, nullptr);
        view = VK_NULL_HANDLE;
    }
    if (environmentMip0Storage_ != VK_NULL_HANDLE) vkDestroyImageView(ctx.device, environmentMip0Storage_, nullptr);
    environmentMip0Storage_ = VK_NULL_HANDLE;
    destroyImage(ctx, dfg_);
    destroyImage(ctx, environment_);
    destroyImage(ctx, prefiltered_);
    destroyBuffer(ctx, sh_);
    dfgReady_ = false;
    captured_ = false;
    lastKey_.clear();
}

std::vector<float> ImageBasedLighting::quantisedKey(const IblInputs& in) const {
    std::vector<float> key;
    key.reserve(24);
    pushRadiance(key, in.zenith);
    pushRadiance(key, in.horizon);
    pushRadiance(key, in.ground);
    glm::vec3 sun = glm::normalize(in.towardSun);
    for (int i = 0; i < 3; ++i) key.push_back(std::round(sun[i] * 256.0f));
    for (int i = 0; i < 3; ++i) key.push_back(std::floor(in.origin[i] / kOriginQuantum));
    key.push_back(in.atmosphere.x);
    key.push_back(std::round(in.atmosphere.y * 64.0f));
    key.push_back(std::round(in.atmosphere.z * 64.0f));
    bool clouds = in.clouds.x > 0.5f;
    key.push_back(clouds ? 1.0f : 0.0f);
    if (clouds) {
        key.push_back(std::round(in.clouds.y * 64.0f));
        key.push_back(std::round(in.clouds.z * 64.0f));
        key.push_back(std::floor(in.clouds.w / kCloudRecaptureSeconds));
    }
    return key;
}

bool ImageBasedLighting::update(VkCommandBuffer cmd, const IblInputs& inputs) {
    if (!dfgReady_) {
        recordDfg(cmd);
        dfgReady_ = true;
    }
    std::vector<float> key = quantisedKey(inputs);
    if (captured_ && key == lastKey_) return false;

    // Earlier frames may still be sampling these images; a pipeline barrier
    // orders against all prior work on the queue, so frames in flight are
    // covered without per-frame copies.
    recordCapture(cmd, inputs);
    recordMipChain(cmd);
    recordPrefilter(cmd);
    recordSh(cmd);
    lastKey_ = std::move(key);
    captured_ = true;
    return true;
}

void ImageBasedLighting::recordDfg(VkCommandBuffer cmd) {
    imageBarrier(cmd, dfg_.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_NONE, 0,
                 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, dfgPass_.pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, dfgPass_.layout, 0, 1, &dfgSet_, 0, nullptr);
    vkCmdDispatch(cmd, divideRoundUp(kDfgSize, 8), divideRoundUp(kDfgSize, 8), 1);
    imageBarrier(cmd, dfg_.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, kShadingStages,
                 VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
}

void ImageBasedLighting::recordCapture(VkCommandBuffer cmd, const IblInputs& in) {
    imageBarrier(cmd, environment_.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, kShadingStages, 0,
                 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, 0, 1);

    CapturePush push{};
    push.zenith = glm::vec4(in.zenith, 0.0f);
    push.horizon = glm::vec4(in.horizon, 0.0f);
    push.sunDir = glm::vec4(glm::normalize(in.towardSun), 0.0f);
    push.origin = glm::vec4(glm::floor(in.origin / kOriginQuantum) * kOriginQuantum, 1.0f);
    push.atmosphere = in.atmosphere;
    push.clouds = in.clouds;
    push.ground = glm::vec4(in.ground, 0.0f);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, capturePass_.pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, capturePass_.layout, 0, 1, &captureSet_, 0,
                            nullptr);
    vkCmdPushConstants(cmd, capturePass_.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
    vkCmdDispatch(cmd, divideRoundUp(kEnvironmentSize, 8), divideRoundUp(kEnvironmentSize, 8), 6);
}

void ImageBasedLighting::recordMipChain(VkCommandBuffer cmd) {
    imageBarrier(cmd, environment_.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                 VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, 0, 1);
    imageBarrier(cmd, environment_.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                 kShadingStages, 0, VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, 1,
                 environmentMips_ - 1);

    for (uint32_t mip = 1; mip < environmentMips_; ++mip) {
        int32_t src = static_cast<int32_t>(kEnvironmentSize >> (mip - 1));
        int32_t dst = static_cast<int32_t>(kEnvironmentSize >> mip);
        VkImageBlit blit{};
        blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip - 1, 0, 6};
        blit.srcOffsets[1] = {src, src, 1};
        blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip, 0, 6};
        blit.dstOffsets[1] = {dst, dst, 1};
        vkCmdBlitImage(cmd, environment_.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, environment_.image,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
        imageBarrier(cmd, environment_.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                     VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_BLIT_BIT,
                     VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
                     mip, 1);
    }

    imageBarrier(cmd, environment_.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_BLIT_BIT,
                 VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                 VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
}

void ImageBasedLighting::recordPrefilter(VkCommandBuffer cmd) {
    imageBarrier(cmd, prefiltered_.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, kShadingStages, 0,
                 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, prefilterPass_.pipeline);
    for (uint32_t mip = 0; mip < kPrefilteredMips; ++mip) {
        PrefilterPush push{};
        push.perceptualRoughness = static_cast<float>(mip) / static_cast<float>(kPrefilteredMips - 1);
        push.sourceSize = static_cast<float>(kEnvironmentSize);
        push.sampleCount = kPrefilterSamples;
        push.sourceMaxMip = static_cast<float>(environmentMips_ - 1);
        uint32_t size = kPrefilteredSize >> mip;
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, prefilterPass_.layout, 0, 1,
                                &prefilterSets_[mip], 0, nullptr);
        vkCmdPushConstants(cmd, prefilterPass_.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
        vkCmdDispatch(cmd, divideRoundUp(size, 8), divideRoundUp(size, 8), 6);
    }
    imageBarrier(cmd, prefiltered_.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, kShadingStages,
                 VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
}

void ImageBasedLighting::recordSh(VkCommandBuffer cmd) {
    bufferBarrier(cmd, sh_.buffer, kShadingStages, VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
                  VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    ShPush push{kShFaceSize, std::log2(static_cast<float>(kEnvironmentSize / kShFaceSize))};
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, shPass_.pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, shPass_.layout, 0, 1, &shSet_, 0, nullptr);
    vkCmdPushConstants(cmd, shPass_.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
    vkCmdDispatch(cmd, 1, 1, 1);
    bufferBarrier(cmd, sh_.buffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                  kShadingStages, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
}

} // namespace engine::core::render
