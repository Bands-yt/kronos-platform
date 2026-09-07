#include "core/ComputePbrPainter.hpp"

#include <fstream>

#include "core/ResourcePaths.hpp"

namespace engine::core {

namespace {

std::vector<uint32_t> readSpirvFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return {};
    std::streamsize size = file.tellg();
    if (size <= 0 || size % 4 != 0) return {};
    file.seekg(0, std::ios::beg);
    std::vector<uint32_t> buffer(static_cast<size_t>(size) / 4);
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) return {};
    return buffer;
}

VkShaderModule createShaderModuleFromSpirv(VkDevice device, const std::vector<uint32_t>& spirv) {
    VkShaderModuleCreateInfo createInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    createInfo.codeSize = spirv.size() * sizeof(uint32_t);
    createInfo.pCode = spirv.data();
    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &createInfo, nullptr, &module) != VK_SUCCESS) return VK_NULL_HANDLE;
    return module;
}

// Same real VkImageMemoryBarrier2/vkCmdPipelineBarrier2 shape
// Texture.cpp's own (file-local) transitionImageLayout() already uses
// -- duplicated here rather than shared across translation units since
// that one is anonymous-namespace-local, same "small enough that a
// second copy costs less than a shared-header seam" call this
// codebase's own convention already makes elsewhere for such small
// couplings.
void transitionImageLayout(VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout,
                            VkAccessFlags2 srcAccess, VkAccessFlags2 dstAccess, VkPipelineStageFlags2 srcStage,
                            VkPipelineStageFlags2 dstStage) {
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
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(cmd, &dep);
}

struct StampPushConstants {
    glm::vec4 color;
    glm::vec2 uvCenter;
    float radiusUv;
    float softness;
};

constexpr uint32_t kLocalSizeX = 16;
constexpr uint32_t kLocalSizeY = 16;
constexpr uint32_t kSculptLocalSizeX = 64;

// Kronos ("3D DCC Modeling Suite" -- true GPU sculpt brushes): matches
// shaders/sculpt_displace.comp's own PushConstants block member-for-
// member -- 3 real vec4s (16-byte aligned, matching std430 push-constant
// packing) followed by plain 4-byte scalars, so the two sides agree on
// every real offset without hand-computed padding.
struct SculptPushConstants {
    glm::vec4 brushCenter;
    glm::vec4 brushNormal;
    glm::vec4 dragDelta;
    float brushRadius;
    float strength;
    float neighborRadius;
    uint32_t vertexCount;
    uint32_t mode;
};

// Same real staging-free approach Texture::updatePixels() could have
// used but doesn't need to (images always go through a staging buffer
// by Vulkan convention) -- a real, host-visible/host-coherent SSBO,
// legitimate here because Studio's own real target meshes are small
// (tens to low hundreds of vertices, the same scale CsgMesh's own O(n^2)
// merge already assumes), so skipping a device-local-plus-staging round
// trip for this buffer is a real, honest simplification, not a
// performance bug waiting to happen at this engine's actual real scale.
bool createHostVisibleBuffer(VmaAllocator allocator, VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer& outBuffer,
                              VmaAllocation& outAllocation, void*& outMappedData) {
    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo resultInfo{};
    if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &outBuffer, &outAllocation, &resultInfo) != VK_SUCCESS) {
        return false;
    }
    outMappedData = resultInfo.pMappedData;
    return true;
}

} // namespace

ComputePbrPainter::~ComputePbrPainter() { destroy(); }

bool ComputePbrPainter::initialize(VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue,
                                    std::string& outError) {
    allocator_ = allocator;
    device_ = device;
    cmdPool_ = cmdPool;
    queue_ = queue;

    std::string shaderDir = resolveResourceDir(executableDirectory(), "shaders", ENGINE_SHADER_DIR);
    std::vector<uint32_t> spirv = readSpirvFile(shaderDir + "/stamp_texture.comp.spv");
    if (spirv.empty()) {
        outError = "ComputePbrPainter::initialize: failed to read compiled stamp_texture.comp.spv from \"" + shaderDir + "\"";
        return false;
    }

    shaderModule_ = createShaderModuleFromSpirv(device_, spirv);
    if (shaderModule_ == VK_NULL_HANDLE) {
        outError = "ComputePbrPainter::initialize: vkCreateShaderModule failed";
        return false;
    }

    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;
    if (vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr, &setLayout_) != VK_SUCCESS) {
        outError = "ComputePbrPainter::initialize: vkCreateDescriptorSetLayout failed";
        return false;
    }

    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1};
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    if (vkCreateDescriptorPool(device_, &poolInfo, nullptr, &descriptorPool_) != VK_SUCCESS) {
        outError = "ComputePbrPainter::initialize: vkCreateDescriptorPool failed";
        return false;
    }

    VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocInfo.descriptorPool = descriptorPool_;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &setLayout_;
    if (vkAllocateDescriptorSets(device_, &allocInfo, &descriptorSet_) != VK_SUCCESS) {
        outError = "ComputePbrPainter::initialize: vkAllocateDescriptorSets failed";
        return false;
    }

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(StampPushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &setLayout_;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
    if (vkCreatePipelineLayout(device_, &pipelineLayoutInfo, nullptr, &pipelineLayout_) != VK_SUCCESS) {
        outError = "ComputePbrPainter::initialize: vkCreatePipelineLayout failed";
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
        outError = "ComputePbrPainter::initialize: vkCreateComputePipelines failed";
        return false;
    }

    return true;
}

void ComputePbrPainter::destroy() {
    if (device_ == VK_NULL_HANDLE) return;
    if (pipeline_ != VK_NULL_HANDLE) vkDestroyPipeline(device_, pipeline_, nullptr);
    if (pipelineLayout_ != VK_NULL_HANDLE) vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
    if (shaderModule_ != VK_NULL_HANDLE) vkDestroyShaderModule(device_, shaderModule_, nullptr);
    if (descriptorPool_ != VK_NULL_HANDLE) vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
    if (setLayout_ != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device_, setLayout_, nullptr);

    if (sculptPipeline_ != VK_NULL_HANDLE) vkDestroyPipeline(device_, sculptPipeline_, nullptr);
    if (sculptPipelineLayout_ != VK_NULL_HANDLE) vkDestroyPipelineLayout(device_, sculptPipelineLayout_, nullptr);
    if (sculptShaderModule_ != VK_NULL_HANDLE) vkDestroyShaderModule(device_, sculptShaderModule_, nullptr);
    if (sculptDescriptorPool_ != VK_NULL_HANDLE) vkDestroyDescriptorPool(device_, sculptDescriptorPool_, nullptr);
    if (sculptSetLayout_ != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device_, sculptSetLayout_, nullptr);

    pipeline_ = VK_NULL_HANDLE;
    pipelineLayout_ = VK_NULL_HANDLE;
    shaderModule_ = VK_NULL_HANDLE;
    descriptorPool_ = VK_NULL_HANDLE;
    setLayout_ = VK_NULL_HANDLE;
    sculptPipeline_ = VK_NULL_HANDLE;
    sculptPipelineLayout_ = VK_NULL_HANDLE;
    sculptShaderModule_ = VK_NULL_HANDLE;
    sculptDescriptorPool_ = VK_NULL_HANDLE;
    sculptSetLayout_ = VK_NULL_HANDLE;
    device_ = VK_NULL_HANDLE;
}

bool ComputePbrPainter::ensureSculptPipeline(std::string& outError) {
    if (sculptPipeline_ != VK_NULL_HANDLE) return true;

    std::string shaderDir = resolveResourceDir(executableDirectory(), "shaders", ENGINE_SHADER_DIR);
    std::vector<uint32_t> spirv = readSpirvFile(shaderDir + "/sculpt_displace.comp.spv");
    if (spirv.empty()) {
        outError = "ComputePbrPainter::ensureSculptPipeline: failed to read compiled sculpt_displace.comp.spv from \"" +
                   shaderDir + "\"";
        return false;
    }

    sculptShaderModule_ = createShaderModuleFromSpirv(device_, spirv);
    if (sculptShaderModule_ == VK_NULL_HANDLE) {
        outError = "ComputePbrPainter::ensureSculptPipeline: vkCreateShaderModule failed";
        return false;
    }

    VkDescriptorSetLayoutBinding bindings[2]{};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = 2;
    layoutInfo.pBindings = bindings;
    if (vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr, &sculptSetLayout_) != VK_SUCCESS) {
        outError = "ComputePbrPainter::ensureSculptPipeline: vkCreateDescriptorSetLayout failed";
        return false;
    }

    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2};
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    if (vkCreateDescriptorPool(device_, &poolInfo, nullptr, &sculptDescriptorPool_) != VK_SUCCESS) {
        outError = "ComputePbrPainter::ensureSculptPipeline: vkCreateDescriptorPool failed";
        return false;
    }

    VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocInfo.descriptorPool = sculptDescriptorPool_;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &sculptSetLayout_;
    if (vkAllocateDescriptorSets(device_, &allocInfo, &sculptDescriptorSet_) != VK_SUCCESS) {
        outError = "ComputePbrPainter::ensureSculptPipeline: vkAllocateDescriptorSets failed";
        return false;
    }

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(SculptPushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &sculptSetLayout_;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
    if (vkCreatePipelineLayout(device_, &pipelineLayoutInfo, nullptr, &sculptPipelineLayout_) != VK_SUCCESS) {
        outError = "ComputePbrPainter::ensureSculptPipeline: vkCreatePipelineLayout failed";
        return false;
    }

    VkPipelineShaderStageCreateInfo stageInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = sculptShaderModule_;
    stageInfo.pName = "main";

    VkComputePipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = sculptPipelineLayout_;
    if (vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &sculptPipeline_) != VK_SUCCESS) {
        outError = "ComputePbrPainter::ensureSculptPipeline: vkCreateComputePipelines failed";
        return false;
    }

    return true;
}

bool ComputePbrPainter::sculpt(const std::vector<glm::vec3>& positions, SculptBrushMode mode, glm::vec3 brushCenter,
                                float brushRadius, float strength, glm::vec3 brushNormal, glm::vec3 dragDelta,
                                float neighborRadius, std::vector<glm::vec3>& outPositions, std::string& outError) {
    if (device_ == VK_NULL_HANDLE) {
        outError = "ComputePbrPainter::sculpt: not initialized";
        return false;
    }
    if (positions.empty()) {
        outError = "ComputePbrPainter::sculpt: empty positions -- real, honest no-op";
        return false;
    }
    if (!ensureSculptPipeline(outError)) return false;

    const VkDeviceSize bufferBytes = static_cast<VkDeviceSize>(positions.size()) * sizeof(glm::vec4);

    VkBuffer inBuffer = VK_NULL_HANDLE, outBuffer = VK_NULL_HANDLE;
    VmaAllocation inAllocation = nullptr, outAllocation = nullptr;
    void* inMapped = nullptr;
    void* outMapped = nullptr;
    if (!createHostVisibleBuffer(allocator_, bufferBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, inBuffer, inAllocation,
                                  inMapped) ||
        !createHostVisibleBuffer(allocator_, bufferBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, outBuffer, outAllocation,
                                  outMapped)) {
        outError = "ComputePbrPainter::sculpt: vmaCreateBuffer failed";
        if (inBuffer != VK_NULL_HANDLE) vmaDestroyBuffer(allocator_, inBuffer, inAllocation);
        if (outBuffer != VK_NULL_HANDLE) vmaDestroyBuffer(allocator_, outBuffer, outAllocation);
        return false;
    }

    auto* inData = static_cast<glm::vec4*>(inMapped);
    for (size_t i = 0; i < positions.size(); ++i) inData[i] = glm::vec4(positions[i], 0.0f);

    VkDescriptorBufferInfo inInfo{inBuffer, 0, VK_WHOLE_SIZE};
    VkDescriptorBufferInfo outInfo{outBuffer, 0, VK_WHOLE_SIZE};
    VkWriteDescriptorSet writes[2]{};
    writes[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    writes[0].dstSet = sculptDescriptorSet_;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[0].pBufferInfo = &inInfo;
    writes[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    writes[1].dstSet = sculptDescriptorSet_;
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].pBufferInfo = &outInfo;
    vkUpdateDescriptorSets(device_, 2, writes, 0, nullptr);

    VkCommandBufferAllocateInfo cmdAllocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cmdAllocInfo.commandPool = cmdPool_;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(device_, &cmdAllocInfo, &cmd) != VK_SUCCESS) {
        outError = "ComputePbrPainter::sculpt: vkAllocateCommandBuffers failed";
        vmaDestroyBuffer(allocator_, inBuffer, inAllocation);
        vmaDestroyBuffer(allocator_, outBuffer, outAllocation);
        return false;
    }

    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, sculptPipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, sculptPipelineLayout_, 0, 1, &sculptDescriptorSet_, 0,
                             nullptr);

    SculptPushConstants pushConstants{};
    pushConstants.brushCenter = glm::vec4(brushCenter, 0.0f);
    pushConstants.brushNormal = glm::vec4(brushNormal, 0.0f);
    pushConstants.dragDelta = glm::vec4(dragDelta, 0.0f);
    pushConstants.brushRadius = brushRadius;
    pushConstants.strength = strength;
    pushConstants.neighborRadius = neighborRadius;
    pushConstants.vertexCount = static_cast<uint32_t>(positions.size());
    pushConstants.mode = static_cast<uint32_t>(mode);
    vkCmdPushConstants(cmd, sculptPipelineLayout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pushConstants), &pushConstants);

    uint32_t groups = (pushConstants.vertexCount + kSculptLocalSizeX - 1) / kSculptLocalSizeX;
    vkCmdDispatch(cmd, groups, 1, 1);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    vkQueueSubmit(queue_, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue_); // one-shot, synchronous -- same tradeoff stamp() itself already documents

    vkFreeCommandBuffers(device_, cmdPool_, 1, &cmd);

    outPositions.resize(positions.size());
    const auto* readback = static_cast<const glm::vec4*>(outMapped);
    for (size_t i = 0; i < positions.size(); ++i) outPositions[i] = glm::vec3(readback[i]);

    vmaDestroyBuffer(allocator_, inBuffer, inAllocation);
    vmaDestroyBuffer(allocator_, outBuffer, outAllocation);
    return true;
}

bool ComputePbrPainter::stamp(const Texture& texture, glm::vec2 uvCenter, float radiusUv, glm::vec4 color, float softness,
                               std::string& outError) {
    if (!isValid()) {
        outError = "ComputePbrPainter::stamp: not initialized";
        return false;
    }
    if (!texture.isValid()) {
        outError = "ComputePbrPainter::stamp: target texture is not valid";
        return false;
    }

    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageView = texture.view();
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = descriptorSet_;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    write.pImageInfo = &imageInfo;
    vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);

    VkCommandBufferAllocateInfo cmdAllocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cmdAllocInfo.commandPool = cmdPool_;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(device_, &cmdAllocInfo, &cmd) != VK_SUCCESS) {
        outError = "ComputePbrPainter::stamp: vkAllocateCommandBuffers failed";
        return false;
    }

    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    // Real transition: the raster pass leaves this texture in
    // SHADER_READ_ONLY_OPTIMAL (see Texture::createStorageImage()'s own
    // header comment); the compute shader needs GENERAL to imageStore()
    // into it.
    transitionImageLayout(cmd, texture.image(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
                           VK_ACCESS_2_SHADER_READ_BIT, VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
                           VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout_, 0, 1, &descriptorSet_, 0, nullptr);

    StampPushConstants pushConstants{color, uvCenter, radiusUv, softness};
    vkCmdPushConstants(cmd, pipelineLayout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pushConstants), &pushConstants);

    uint32_t groupsX = (static_cast<uint32_t>(texture.width()) + kLocalSizeX - 1) / kLocalSizeX;
    uint32_t groupsY = (static_cast<uint32_t>(texture.height()) + kLocalSizeY - 1) / kLocalSizeY;
    vkCmdDispatch(cmd, groupsX, groupsY, 1);

    // Real transition back -- so the raster pass can keep sampling this
    // texture immediately after this stamp, no separate "finalize"
    // step the caller has to remember.
    transitionImageLayout(cmd, texture.image(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                           VK_ACCESS_2_SHADER_WRITE_BIT, VK_ACCESS_2_SHADER_READ_BIT,
                           VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    vkQueueSubmit(queue_, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue_); // one-shot, synchronous -- see this class's own header comment

    vkFreeCommandBuffers(device_, cmdPool_, 1, &cmd);
    return true;
}

} // namespace engine::core
