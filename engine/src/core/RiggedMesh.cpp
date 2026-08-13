#include "core/RiggedMesh.hpp"

#include <algorithm>
#include <cstring>

namespace engine::core {

VkVertexInputBindingDescription GpuSkinVertex::bindingDescription() {
    VkVertexInputBindingDescription binding{};
    binding.binding = 1; // binding 0 is always the base Vertex buffer, see Mesh.hpp
    binding.stride = sizeof(GpuSkinVertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX; // per-vertex, not per-instance -- skin data is authored per mesh vertex
    return binding;
}

std::vector<VkVertexInputAttributeDescription> GpuSkinVertex::attributeDescriptions(uint32_t startLocation) {
    std::vector<VkVertexInputAttributeDescription> attributes(2);
    attributes[0].binding = 1;
    attributes[0].location = startLocation;
    attributes[0].format = VK_FORMAT_R32G32B32A32_SINT; // ivec4
    attributes[0].offset = offsetof(GpuSkinVertex, jointIndices);

    attributes[1].binding = 1;
    attributes[1].location = startLocation + 1;
    attributes[1].format = VK_FORMAT_R32G32B32A32_SFLOAT; // vec4
    attributes[1].offset = offsetof(GpuSkinVertex, weights);
    return attributes;
}

namespace {

// Same "staging buffer -> device-local, one-shot synchronous submit"
// pattern core::Mesh.cpp's own (file-local) uploadToDeviceLocalBuffer()
// uses -- duplicated here rather than shared, matching how this
// codebase has no common VulkanUtils translation unit today (Texture.cpp
// has its own copy too).
bool createBuffer(VmaAllocator allocator, VkDeviceSize size, VkBufferUsageFlags usage, VmaMemoryUsage memUsage,
                   VmaAllocationCreateFlags flags, VkBuffer& outBuffer, VmaAllocation& outAllocation) {
    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = memUsage;
    allocInfo.flags = flags;

    return vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &outBuffer, &outAllocation, nullptr) == VK_SUCCESS;
}

bool uploadToDeviceLocalBuffer(VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue,
                                const void* data, VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer& outBuffer,
                                VmaAllocation& outAllocation) {
    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VmaAllocation stagingAllocation = nullptr;
    if (!createBuffer(allocator, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_AUTO,
                       VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
                       stagingBuffer, stagingAllocation)) {
        return false;
    }

    VmaAllocationInfo stagingInfo{};
    vmaGetAllocationInfo(allocator, stagingAllocation, &stagingInfo);
    std::memcpy(stagingInfo.pMappedData, data, static_cast<size_t>(size));

    if (!createBuffer(allocator, size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | usage, VMA_MEMORY_USAGE_AUTO, 0, outBuffer,
                       outAllocation)) {
        vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
        return false;
    }

    VkCommandBufferAllocateInfo cmdAllocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cmdAllocInfo.commandPool = cmdPool;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    // Kronos (Phase 1 stability audit fix): real result check -- see
    // core::uploadToDeviceLocalBuffer() in Mesh.cpp for the identical
    // real bug this mirrors (unchecked allocation -> UB on the null cmd).
    if (vkAllocateCommandBuffers(device, &cmdAllocInfo, &cmd) != VK_SUCCESS) {
        vmaDestroyBuffer(allocator, outBuffer, outAllocation);
        vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
        return false;
    }

    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    VkBufferCopy copyRegion{0, 0, size};
    vkCmdCopyBuffer(cmd, stagingBuffer, outBuffer, 1, &copyRegion);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);

    vkFreeCommandBuffers(device, cmdPool, 1, &cmd);
    vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
    return true;
}

} // namespace

bool RiggedMesh::uploadFromHost(VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue,
                                 const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices,
                                 const SkinWeights& skinWeights, Skeleton skeleton, std::string& outError) {
    if (!skeleton.validate(outError)) return false;
    if (!skinWeights.validate(vertices.size(), static_cast<int>(skeleton.joints.size()), outError)) return false;

    if (!mesh_.uploadFromHost(allocator, device, cmdPool, queue, vertices, indices)) {
        outError = "GPU upload failed for the base mesh (vertex/index buffers)";
        return false;
    }

    std::vector<GpuSkinVertex> gpuSkin(skinWeights.perVertex.size());
    for (size_t i = 0; i < skinWeights.perVertex.size(); ++i) {
        const VertexSkinWeights& src = skinWeights.perVertex[i];
        gpuSkin[i].jointIndices = glm::ivec4(src.jointIndices[0], src.jointIndices[1], src.jointIndices[2], src.jointIndices[3]);
        gpuSkin[i].weights = glm::vec4(src.weights[0], src.weights[1], src.weights[2], src.weights[3]);
    }

    if (!uploadToDeviceLocalBuffer(allocator, device, cmdPool, queue, gpuSkin.data(),
                                    sizeof(GpuSkinVertex) * gpuSkin.size(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                    skinBuffer_, skinAllocation_)) {
        outError = "GPU upload failed for the skin-weight buffer";
        mesh_.destroy(allocator);
        return false;
    }

    skeleton_ = std::move(skeleton);
    return true;
}

void RiggedMesh::destroy(VmaAllocator allocator) {
    mesh_.destroy(allocator);
    if (skinBuffer_ != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator, skinBuffer_, skinAllocation_);
        skinBuffer_ = VK_NULL_HANDLE;
        skinAllocation_ = nullptr;
    }
}

std::vector<Vertex> skinVerticesCPU(const std::vector<Vertex>& vertices, const SkinWeights& skinWeights,
                                     const std::vector<glm::mat4>& skinningMatrices) {
    std::vector<Vertex> result = vertices;
    size_t count = std::min(vertices.size(), skinWeights.perVertex.size());

    for (size_t i = 0; i < count; ++i) {
        const VertexSkinWeights& sw = skinWeights.perVertex[i];
        glm::vec3 skinnedPosition(0.0f);
        glm::vec3 skinnedNormal(0.0f);

        for (int k = 0; k < 4; ++k) {
            int jointIndex = sw.jointIndices[static_cast<size_t>(k)];
            float weight = sw.weights[static_cast<size_t>(k)];
            if (jointIndex < 0 || weight <= 0.0f) continue;
            if (jointIndex >= static_cast<int>(skinningMatrices.size())) continue; // already validated elsewhere; defensive here

            const glm::mat4& skinMatrix = skinningMatrices[static_cast<size_t>(jointIndex)];
            skinnedPosition += weight * glm::vec3(skinMatrix * glm::vec4(vertices[i].position, 1.0f));
            skinnedNormal += weight * glm::mat3(skinMatrix) * vertices[i].normal;
        }

        result[i].position = skinnedPosition;
        result[i].normal = glm::length(skinnedNormal) > 1e-8f ? glm::normalize(skinnedNormal) : vertices[i].normal;
        // uv unchanged; tangent is recomputed automatically by
        // Mesh::uploadFromHost()'s computeTangents() pass once this
        // result is uploaded, so it's left as-is here (see RiggedMesh.hpp's
        // doc comment).
    }
    return result;
}

uint32_t RiggedMeshLibrary::registerRiggedMesh(RiggedMesh riggedMesh) {
    riggedMeshes_.push_back(std::move(riggedMesh));
    return static_cast<uint32_t>(riggedMeshes_.size()) - 1;
}

const RiggedMesh* RiggedMeshLibrary::get(uint32_t handle) const {
    if (handle >= riggedMeshes_.size()) return nullptr;
    return &riggedMeshes_[handle];
}

void RiggedMeshLibrary::destroyAll(VmaAllocator allocator) {
    for (auto& riggedMesh : riggedMeshes_) riggedMesh.destroy(allocator);
    riggedMeshes_.clear();
}

} // namespace engine::core
