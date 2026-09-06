#include "core/GpuParticleCompute.hpp"

#include <cstring>

namespace engine::core {

namespace {

VkShaderModule createShaderModuleFromSpirv(VkDevice device, const std::vector<uint32_t>& spirv) {
    VkShaderModuleCreateInfo createInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    createInfo.codeSize = spirv.size() * sizeof(uint32_t);
    createInfo.pCode = spirv.data();
    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &createInfo, nullptr, &module) != VK_SUCCESS) return VK_NULL_HANDLE;
    return module;
}

struct ParticlePushConstants {
    float deltaTime;
    uint32_t particleCount;
};

} // namespace

GpuParticleCompute::~GpuParticleCompute() { destroy(); }

bool GpuParticleCompute::initialize(VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue,
                                     const std::vector<uint32_t>& spirv, uint32_t maxParticles, std::string& outError) {
    if (spirv.empty()) {
        outError = "GpuParticleCompute::initialize: empty SPIR-V";
        return false;
    }
    if (maxParticles == 0) {
        outError = "GpuParticleCompute::initialize: maxParticles must be > 0";
        return false;
    }

    allocator_ = allocator;
    device_ = device;
    cmdPool_ = cmdPool;
    queue_ = queue;
    maxParticles_ = maxParticles;

    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = static_cast<VkDeviceSize>(maxParticles) * sizeof(GpuParticle);
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo vmaAllocInfo{};
    vmaAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    // HOST_ACCESS_RANDOM (not SEQUENTIAL_WRITE, see UIRenderer.cpp's own
    // buffer for that convention) -- stepParticles() both writes
    // (upload) AND reads (readback) this same buffer from the host,
    // unlike UIRenderer's own write-only per-frame vertex buffer.
    vmaAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo resultInfo{};
    if (vmaCreateBuffer(allocator_, &bufferInfo, &vmaAllocInfo, &particleBuffer_, &particleBufferAllocation_,
                         &resultInfo) != VK_SUCCESS) {
        outError = "GpuParticleCompute::initialize: vmaCreateBuffer failed";
        return false;
    }
    particleBufferMapped_ = resultInfo.pMappedData;

    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;
    if (vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr, &setLayout_) != VK_SUCCESS) {
        outError = "GpuParticleCompute::initialize: vkCreateDescriptorSetLayout failed";
        return false;
    }

    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1};
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    if (vkCreateDescriptorPool(device_, &poolInfo, nullptr, &descriptorPool_) != VK_SUCCESS) {
        outError = "GpuParticleCompute::initialize: vkCreateDescriptorPool failed";
        return false;
    }

    VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocInfo.descriptorPool = descriptorPool_;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &setLayout_;
    if (vkAllocateDescriptorSets(device_, &allocInfo, &descriptorSet_) != VK_SUCCESS) {
        outError = "GpuParticleCompute::initialize: vkAllocateDescriptorSets failed";
        return false;
    }

    VkDescriptorBufferInfo bufferDescInfo{};
    bufferDescInfo.buffer = particleBuffer_;
    bufferDescInfo.offset = 0;
    bufferDescInfo.range = VK_WHOLE_SIZE;
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = descriptorSet_;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.pBufferInfo = &bufferDescInfo;
    vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);

    shaderModule_ = createShaderModuleFromSpirv(device_, spirv);
    if (shaderModule_ == VK_NULL_HANDLE) {
        outError = "GpuParticleCompute::initialize: vkCreateShaderModule failed (invalid SPIR-V?)";
        return false;
    }

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(ParticlePushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &setLayout_;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
    if (vkCreatePipelineLayout(device_, &pipelineLayoutInfo, nullptr, &pipelineLayout_) != VK_SUCCESS) {
        outError = "GpuParticleCompute::initialize: vkCreatePipelineLayout failed";
        return false;
    }

    VkPipelineShaderStageCreateInfo stageInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = shaderModule_;
    stageInfo.pName = "main";

    VkComputePipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = pipelineLayout_;
    if (vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline_) != VK_SUCCESS) {
        outError = "GpuParticleCompute::initialize: vkCreateComputePipelines failed";
        return false;
    }

    return true;
}

void GpuParticleCompute::destroy() {
    if (device_ == VK_NULL_HANDLE) return;
    if (pipeline_ != VK_NULL_HANDLE) vkDestroyPipeline(device_, pipeline_, nullptr);
    if (pipelineLayout_ != VK_NULL_HANDLE) vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
    if (shaderModule_ != VK_NULL_HANDLE) vkDestroyShaderModule(device_, shaderModule_, nullptr);
    if (descriptorPool_ != VK_NULL_HANDLE) vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
    if (setLayout_ != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device_, setLayout_, nullptr);
    if (particleBuffer_ != VK_NULL_HANDLE) vmaDestroyBuffer(allocator_, particleBuffer_, particleBufferAllocation_);

    pipeline_ = VK_NULL_HANDLE;
    pipelineLayout_ = VK_NULL_HANDLE;
    shaderModule_ = VK_NULL_HANDLE;
    descriptorPool_ = VK_NULL_HANDLE;
    setLayout_ = VK_NULL_HANDLE;
    particleBuffer_ = VK_NULL_HANDLE;
    particleBufferAllocation_ = nullptr;
    particleBufferMapped_ = nullptr;
    device_ = VK_NULL_HANDLE;
}

bool GpuParticleCompute::stepParticles(std::vector<GpuParticle>& particles, float deltaTime, std::string& outError) {
    if (!isValid()) {
        outError = "GpuParticleCompute::stepParticles: not initialized";
        return false;
    }
    if (particles.size() > maxParticles_) {
        outError = "GpuParticleCompute::stepParticles: particles.size() exceeds maxParticles this instance was initialized with";
        return false;
    }
    if (particles.empty()) return true; // real, honest no-op -- nothing to integrate

    uint32_t particleCount = static_cast<uint32_t>(particles.size());
    std::memcpy(particleBufferMapped_, particles.data(), particleCount * sizeof(GpuParticle));
    vmaFlushAllocation(allocator_, particleBufferAllocation_, 0, VK_WHOLE_SIZE);

    VkCommandBufferAllocateInfo cmdAllocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cmdAllocInfo.commandPool = cmdPool_;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(device_, &cmdAllocInfo, &cmd) != VK_SUCCESS) {
        outError = "GpuParticleCompute::stepParticles: vkAllocateCommandBuffers failed";
        return false;
    }

    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    // Real barrier: the host write above must be visible to the compute
    // shader's own read/write before it runs -- see this class's own
    // header comment on why this is a real, one-shot, synchronous round
    // trip, matching Texture::loadFromFile()'s own staging-upload
    // pattern.
    VkBufferMemoryBarrier hostToCompute{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    hostToCompute.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
    hostToCompute.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    hostToCompute.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    hostToCompute.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    hostToCompute.buffer = particleBuffer_;
    hostToCompute.offset = 0;
    hostToCompute.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1,
                          &hostToCompute, 0, nullptr);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout_, 0, 1, &descriptorSet_, 0, nullptr);

    ParticlePushConstants pushConstants{deltaTime, particleCount};
    vkCmdPushConstants(cmd, pipelineLayout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pushConstants), &pushConstants);

    uint32_t groupCount = (particleCount + kLocalSizeX - 1) / kLocalSizeX;
    vkCmdDispatch(cmd, groupCount, 1, 1);

    // Real barrier: the compute shader's own write must be visible to
    // the host read below, before we ever touch particleBufferMapped_
    // again.
    VkBufferMemoryBarrier computeToHost{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    computeToHost.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    computeToHost.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    computeToHost.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    computeToHost.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    computeToHost.buffer = particleBuffer_;
    computeToHost.offset = 0;
    computeToHost.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1,
                          &computeToHost, 0, nullptr);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    vkQueueSubmit(queue_, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue_); // one-shot, synchronous -- same tradeoff Texture::loadFromFile()/Mesh::uploadFromHost() make

    vkFreeCommandBuffers(device_, cmdPool_, 1, &cmd);

    vmaInvalidateAllocation(allocator_, particleBufferAllocation_, 0, VK_WHOLE_SIZE);
    std::memcpy(particles.data(), particleBufferMapped_, particleCount * sizeof(GpuParticle));

    return true;
}

} // namespace engine::core
