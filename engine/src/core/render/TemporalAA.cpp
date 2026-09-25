#include "core/render/TemporalAA.hpp"

namespace engine::core::render {

namespace {

constexpr VkFormat kHistoryFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

struct TaaPush {
    glm::vec2 invSize;
    float feedback;
    float resetHistory;
};

float halton(uint32_t index, uint32_t base) {
    float f = 1.0f;
    float r = 0.0f;
    while (index > 0) {
        f /= static_cast<float>(base);
        r += f * static_cast<float>(index % base);
        index /= base;
    }
    return r;
}

VkSampler makeSampler(VkDevice device, VkFilter filter) {
    VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    info.magFilter = filter;
    info.minFilter = filter;
    info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    VkSampler sampler = VK_NULL_HANDLE;
    vkCreateSampler(device, &info, nullptr, &sampler);
    return sampler;
}

} // namespace

bool TemporalAA::initialize(const GpuContext& ctx, uint32_t maxBindings) {
    pointSampler_ = makeSampler(ctx.device, VK_FILTER_NEAREST);
    linearSampler_ = makeSampler(ctx.device, VK_FILTER_LINEAR);
    if (pointSampler_ == VK_NULL_HANDLE || linearSampler_ == VK_NULL_HANDLE) return false;

    std::array<VkDescriptorSetLayoutBinding, 5> bindings{};
    for (uint32_t i = 0; i < bindings.size(); ++i) {
        bindings[i].binding = i;
        bindings[i].descriptorType = i < 4 ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo setInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    setInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    setInfo.pBindings = bindings.data();
    if (vkCreateDescriptorSetLayout(ctx.device, &setInfo, nullptr, &setLayout_) != VK_SUCCESS) return false;

    VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(TaaPush)};
    VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &setLayout_;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &range;
    if (vkCreatePipelineLayout(ctx.device, &layoutInfo, nullptr, &layout_) != VK_SUCCESS) return false;
    pipeline_ = createComputePipeline(ctx, layout_, "taa_resolve.comp.spv");
    if (pipeline_ == VK_NULL_HANDLE) return false;

    uint32_t setCount = maxBindings * 2;
    std::array<VkDescriptorPoolSize, 2> sizes{{
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, setCount * 4},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, setCount},
    }};
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = setCount;
    poolInfo.poolSizeCount = static_cast<uint32_t>(sizes.size());
    poolInfo.pPoolSizes = sizes.data();
    return vkCreateDescriptorPool(ctx.device, &poolInfo, nullptr, &pool_) == VK_SUCCESS;
}

void TemporalAA::shutdown(const GpuContext& ctx) {
    if (pool_ != VK_NULL_HANDLE) vkDestroyDescriptorPool(ctx.device, pool_, nullptr);
    if (pipeline_ != VK_NULL_HANDLE) vkDestroyPipeline(ctx.device, pipeline_, nullptr);
    if (layout_ != VK_NULL_HANDLE) vkDestroyPipelineLayout(ctx.device, layout_, nullptr);
    if (setLayout_ != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(ctx.device, setLayout_, nullptr);
    if (pointSampler_ != VK_NULL_HANDLE) vkDestroySampler(ctx.device, pointSampler_, nullptr);
    if (linearSampler_ != VK_NULL_HANDLE) vkDestroySampler(ctx.device, linearSampler_, nullptr);
    *this = TemporalAA{};
}

glm::vec2 TemporalAA::jitterNdc(uint64_t frameIndex, VkExtent2D extent) {
    uint32_t i = static_cast<uint32_t>(frameIndex % kJitterPhases) + 1;
    glm::vec2 pixelOffset(halton(i, 2) - 0.5f, halton(i, 3) - 0.5f);
    return pixelOffset * glm::vec2(2.0f / static_cast<float>(extent.width), 2.0f / static_cast<float>(extent.height));
}

bool TemporalAA::ensureHistory(const GpuContext& ctx, ViewHistory& view, VkExtent2D extent) const {
    if (view.history[0].image != VK_NULL_HANDLE && view.extent.width == extent.width &&
        view.extent.height == extent.height) {
        return true;
    }
    if (view.history[0].image != VK_NULL_HANDLE) vkDeviceWaitIdle(ctx.device);
    for (GpuImage& image : view.history) destroyImage(ctx, image);

    constexpr VkImageUsageFlags kUsage =
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    for (GpuImage& image : view.history) {
        if (!createImage(ctx, kHistoryFormat, extent.width, extent.height, 1, 1, false, kUsage, image)) return false;
    }
    view.extent = extent;
    ++view.generation;
    view.readIndex = 0;
    view.historyValid = false;
    view.layoutsInitialised = false;
    return true;
}

void TemporalAA::destroyHistory(const GpuContext& ctx, ViewHistory& view) {
    for (GpuImage& image : view.history) destroyImage(ctx, image);
    view.extent = {0, 0};
    view.historyValid = false;
    view.layoutsInitialised = false;
}

bool TemporalAA::bind(const GpuContext& ctx, TaaBinding& binding, const ViewHistory& view, VkImageView currentColor,
                      VkImageView velocity, VkImageView depth) {
    if (binding.boundCurrent == currentColor && binding.boundVelocity == velocity && binding.boundDepth == depth &&
        binding.boundHistory == &view && binding.boundGeneration == view.generation) {
        return true;
    }
    if (binding.sets[0] == VK_NULL_HANDLE) {
        std::array<VkDescriptorSetLayout, 2> layouts{setLayout_, setLayout_};
        VkDescriptorSetAllocateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        info.descriptorPool = pool_;
        info.descriptorSetCount = 2;
        info.pSetLayouts = layouts.data();
        if (vkAllocateDescriptorSets(ctx.device, &info, binding.sets.data()) != VK_SUCCESS) return false;
    }

    // Set i reads history[i] and writes history[1 - i].
    std::array<std::array<VkDescriptorImageInfo, 5>, 2> infos{};
    std::array<VkWriteDescriptorSet, 10> writes{};
    for (uint32_t s = 0; s < 2; ++s) {
        infos[s][0] = {pointSampler_, currentColor, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        infos[s][1] = {linearSampler_, view.history[s].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        infos[s][2] = {pointSampler_, velocity, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        infos[s][3] = {pointSampler_, depth, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        infos[s][4] = {VK_NULL_HANDLE, view.history[1 - s].view, VK_IMAGE_LAYOUT_GENERAL};
        for (uint32_t b = 0; b < 5; ++b) {
            VkWriteDescriptorSet& w = writes[s * 5 + b];
            w = VkWriteDescriptorSet{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            w.dstSet = binding.sets[s];
            w.dstBinding = b;
            w.descriptorCount = 1;
            w.descriptorType = b < 4 ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            w.pImageInfo = &infos[s][b];
        }
    }
    vkUpdateDescriptorSets(ctx.device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

    binding.boundCurrent = currentColor;
    binding.boundVelocity = velocity;
    binding.boundDepth = depth;
    binding.boundHistory = &view;
    binding.boundGeneration = view.generation;
    return true;
}

void TemporalAA::releaseBinding(const GpuContext& ctx, TaaBinding& binding) {
    if (binding.sets[0] != VK_NULL_HANDLE && pool_ != VK_NULL_HANDLE) {
        vkFreeDescriptorSets(ctx.device, pool_, 2, binding.sets.data());
    }
    binding = TaaBinding{};
}

void TemporalAA::resolve(VkCommandBuffer cmd, const TaaBinding& binding, ViewHistory& view, VkImage targetImage,
                         float feedback) const {
    uint32_t read = view.readIndex;
    uint32_t write = 1 - read;
    VkImage readImage = view.history[read].image;
    VkImage writeImage = view.history[write].image;

    if (!view.layoutsInitialised) {
        imageBarrier(cmd, readImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                     VK_PIPELINE_STAGE_2_NONE, 0, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                     VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        view.layoutsInitialised = true;
    }
    imageBarrier(cmd, writeImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, 0, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                 VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);

    TaaPush push{};
    push.invSize = glm::vec2(1.0f / static_cast<float>(view.extent.width), 1.0f / static_cast<float>(view.extent.height));
    push.feedback = feedback;
    push.resetHistory = view.historyValid ? 0.0f : 1.0f;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout_, 0, 1, &binding.sets[read], 0, nullptr);
    vkCmdPushConstants(cmd, layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
    vkCmdDispatch(cmd, divideRoundUp(view.extent.width, 8), divideRoundUp(view.extent.height, 8), 1);

    imageBarrier(cmd, writeImage, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                 VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
    imageBarrier(cmd, targetImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                 VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);

    VkImageCopy region{};
    region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.extent = {view.extent.width, view.extent.height, 1};
    vkCmdCopyImage(cmd, writeImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, targetImage,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    imageBarrier(cmd, writeImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                 VK_PIPELINE_STAGE_2_COPY_BIT, 0, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                 VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    imageBarrier(cmd, targetImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                 VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                 VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                 VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

    view.readIndex = write;
    view.historyValid = true;
}

} // namespace engine::core::render
