#include "core/render/MotionBlur.hpp"

#include <array>

namespace engine::core::render {

namespace {

// RGBA rather than RG so the storage write needs no extended-format support.
constexpr VkFormat kTileFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

VkDescriptorSetLayout makeSetLayout(VkDevice device, VkDescriptorType secondType, VkShaderStageFlags stages) {
    std::array<VkDescriptorSetLayoutBinding, 2> bindings{};
    for (uint32_t i = 0; i < bindings.size(); ++i) {
        bindings[i].binding = i;
        bindings[i].descriptorType = i == 0 ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER : secondType;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = stages;
    }
    VkDescriptorSetLayoutCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    info.bindingCount = static_cast<uint32_t>(bindings.size());
    info.pBindings = bindings.data();
    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    vkCreateDescriptorSetLayout(device, &info, nullptr, &layout);
    return layout;
}

} // namespace

bool MotionBlur::initialize(const GpuContext& ctx, uint32_t maxBindings) {
    VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(ctx.device, &samplerInfo, nullptr, &pointSampler_) != VK_SUCCESS) return false;

    tileSetLayout_ = makeSetLayout(ctx.device, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT);
    sampleSetLayout_ =
        makeSetLayout(ctx.device, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);
    if (tileSetLayout_ == VK_NULL_HANDLE || sampleSetLayout_ == VK_NULL_HANDLE) return false;

    VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &tileSetLayout_;
    if (vkCreatePipelineLayout(ctx.device, &layoutInfo, nullptr, &tileLayout_) != VK_SUCCESS) return false;
    tilePipeline_ = createComputePipeline(ctx, tileLayout_, "motion_tiles.comp.spv");
    if (tilePipeline_ == VK_NULL_HANDLE) return false;

    std::array<VkDescriptorPoolSize, 2> sizes{{
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, maxBindings * 3},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, maxBindings},
    }};
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = maxBindings * 2;
    poolInfo.poolSizeCount = static_cast<uint32_t>(sizes.size());
    poolInfo.pPoolSizes = sizes.data();
    return vkCreateDescriptorPool(ctx.device, &poolInfo, nullptr, &pool_) == VK_SUCCESS;
}

void MotionBlur::shutdown(const GpuContext& ctx) {
    if (pool_ != VK_NULL_HANDLE) vkDestroyDescriptorPool(ctx.device, pool_, nullptr);
    if (tilePipeline_ != VK_NULL_HANDLE) vkDestroyPipeline(ctx.device, tilePipeline_, nullptr);
    if (tileLayout_ != VK_NULL_HANDLE) vkDestroyPipelineLayout(ctx.device, tileLayout_, nullptr);
    if (tileSetLayout_ != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(ctx.device, tileSetLayout_, nullptr);
    if (sampleSetLayout_ != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(ctx.device, sampleSetLayout_, nullptr);
    if (pointSampler_ != VK_NULL_HANDLE) vkDestroySampler(ctx.device, pointSampler_, nullptr);
    *this = MotionBlur{};
}

bool MotionBlur::bind(const GpuContext& ctx, MotionBlurBinding& binding, VkExtent2D extent, VkImageView velocity) {
    const bool sizeMatches = binding.tiles.image != VK_NULL_HANDLE && binding.extent.width == extent.width &&
                             binding.extent.height == extent.height;
    if (sizeMatches && binding.boundVelocity == velocity) return true;

    if (!sizeMatches) {
        if (binding.tiles.image != VK_NULL_HANDLE) vkDeviceWaitIdle(ctx.device);
        destroyImage(ctx, binding.tiles);
        if (!createImage(ctx, kTileFormat, divideRoundUp(extent.width, kMotionBlurTileSize),
                         divideRoundUp(extent.height, kMotionBlurTileSize), 1, 1, false,
                         VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, binding.tiles)) {
            binding = MotionBlurBinding{};
            return false;
        }
        binding.extent = extent;
        binding.tilesReadable = false;
    }

    if (binding.tileSet == VK_NULL_HANDLE) {
        std::array<VkDescriptorSetLayout, 2> layouts{tileSetLayout_, sampleSetLayout_};
        std::array<VkDescriptorSet, 2> sets{};
        VkDescriptorSetAllocateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        info.descriptorPool = pool_;
        info.descriptorSetCount = static_cast<uint32_t>(layouts.size());
        info.pSetLayouts = layouts.data();
        if (vkAllocateDescriptorSets(ctx.device, &info, sets.data()) != VK_SUCCESS) return false;
        binding.tileSet = sets[0];
        binding.sampleSet = sets[1];
    }

    const VkDescriptorImageInfo velocityInfo{pointSampler_, velocity, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    const VkDescriptorImageInfo tileStorageInfo{VK_NULL_HANDLE, binding.tiles.view, VK_IMAGE_LAYOUT_GENERAL};
    const VkDescriptorImageInfo tileSampleInfo{pointSampler_, binding.tiles.view,
                                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    auto write = [](VkDescriptorSet set, uint32_t dstBinding, VkDescriptorType type,
                    const VkDescriptorImageInfo* image) {
        VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w.dstSet = set;
        w.dstBinding = dstBinding;
        w.descriptorCount = 1;
        w.descriptorType = type;
        w.pImageInfo = image;
        return w;
    };
    const std::array<VkWriteDescriptorSet, 4> writes{
        write(binding.tileSet, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &velocityInfo),
        write(binding.tileSet, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &tileStorageInfo),
        write(binding.sampleSet, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &velocityInfo),
        write(binding.sampleSet, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &tileSampleInfo),
    };
    vkUpdateDescriptorSets(ctx.device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    binding.boundVelocity = velocity;
    return true;
}

void MotionBlur::releaseBinding(const GpuContext& ctx, MotionBlurBinding& binding) {
    if (binding.tileSet != VK_NULL_HANDLE && pool_ != VK_NULL_HANDLE) {
        std::array<VkDescriptorSet, 2> sets{binding.tileSet, binding.sampleSet};
        vkFreeDescriptorSets(ctx.device, pool_, static_cast<uint32_t>(sets.size()), sets.data());
    }
    destroyImage(ctx, binding.tiles);
    binding = MotionBlurBinding{};
}

void MotionBlur::prepareTiles(VkCommandBuffer cmd, MotionBlurBinding& binding, bool compute) const {
    if (!compute) {
        if (!binding.tilesReadable) {
            imageBarrier(cmd, binding.tiles.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                         VK_PIPELINE_STAGE_2_NONE, 0, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                         VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
            binding.tilesReadable = true;
        }
        return;
    }

    imageBarrier(cmd, binding.tiles.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                 VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                 VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, tilePipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, tileLayout_, 0, 1, &binding.tileSet, 0, nullptr);
    vkCmdDispatch(cmd, divideRoundUp(binding.extent.width, kMotionBlurTileSize),
                  divideRoundUp(binding.extent.height, kMotionBlurTileSize), 1);
    imageBarrier(cmd, binding.tiles.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                 VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    binding.tilesReadable = true;
}

} // namespace engine::core::render
