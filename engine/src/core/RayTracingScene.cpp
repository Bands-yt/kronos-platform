#include "core/RayTracingScene.hpp"

#include <algorithm>
#include <cstring>
#include <unordered_set>

#include "core/Logger.hpp"
#include "core/RiggedMesh.hpp"

namespace engine::core {

namespace {

constexpr VkDeviceSize kUploadAlignment = 256;
constexpr uint32_t kBlasIdleRecords = 120;
constexpr uint32_t kSkinningGroupSize = 64;

struct SkinningPush {
    glm::uvec2 source;
    glm::uvec2 skin;
    glm::uvec2 palette;
    glm::uvec2 destination;
    uint32_t vertexCount;
};

VkDeviceSize alignUp(VkDeviceSize value, VkDeviceSize alignment) { return (value + alignment - 1) & ~(alignment - 1); }

glm::uvec2 splitAddress(VkDeviceAddress address) {
    return glm::uvec2(static_cast<uint32_t>(address & 0xFFFFFFFFu), static_cast<uint32_t>(address >> 32));
}

void memoryBarrier(VkCommandBuffer cmd, VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                   VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess) {
    VkMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    barrier.srcStageMask = srcStage;
    barrier.srcAccessMask = srcAccess;
    barrier.dstStageMask = dstStage;
    barrier.dstAccessMask = dstAccess;
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.memoryBarrierCount = 1;
    dependency.pMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(cmd, &dependency);
}

VkAccelerationStructureGeometryKHR triangleGeometry(VkDeviceAddress vertices, uint32_t vertexCount,
                                                   VkDeviceAddress indices) {
    VkAccelerationStructureGeometryKHR geometry{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
    geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    auto& triangles = geometry.geometry.triangles;
    triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    triangles.vertexData.deviceAddress = vertices;
    triangles.vertexStride = sizeof(Vertex);
    triangles.maxVertex = vertexCount > 0 ? vertexCount - 1 : 0;
    triangles.indexType = VK_INDEX_TYPE_UINT32;
    triangles.indexData.deviceAddress = indices;
    return geometry;
}

} // namespace

RtInstanceData makeRtMaterial(glm::vec4 baseColor, float metallic, float roughness, glm::vec3 emissiveColor,
                              float emissiveIntensity, uint32_t albedoSlot) {
    RtInstanceData data;
    data.baseColor = baseColor;
    data.surface = glm::vec4(std::clamp(metallic, 0.0f, 1.0f), std::clamp(roughness, 0.0f, 1.0f), 0.0f, 0.0f);
    data.emissive = glm::vec4(emissiveColor * std::max(emissiveIntensity, 0.0f), 0.0f);
    data.textures.x = albedoSlot;
    return data;
}

VkTransformMatrixKHR RayTracingScene::toVkTransform(const glm::mat4& m) {
    VkTransformMatrixKHR out{};
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 4; ++col) out.matrix[row][col] = m[col][row];
    }
    return out;
}

RayTracingScene::~RayTracingScene() { shutdown(); }

bool RayTracingScene::initialize(VmaAllocator allocator, VkDevice device, VkPhysicalDevice physicalDevice,
                                 uint32_t queueFamilyIndex, VkQueue queue) {
    allocator_ = allocator;
    device_ = device;
    queue_ = queue;

    VkPhysicalDeviceAccelerationStructurePropertiesKHR asProperties{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR};
    VkPhysicalDeviceProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    properties.pNext = &asProperties;
    vkGetPhysicalDeviceProperties2(physicalDevice, &properties);
    scratchAlignment_ = std::max<VkDeviceSize>(asProperties.minAccelerationStructureScratchOffsetAlignment, 1);

    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT | VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    poolInfo.queueFamilyIndex = queueFamilyIndex;
    if (vkCreateCommandPool(device_, &poolInfo, nullptr, &cmdPool_) != VK_SUCCESS) {
        logError("RayTracingScene", "vkCreateCommandPool failed.");
        return false;
    }
    initialized_ = true;
    return true;
}

bool RayTracingScene::initializeSkinning(const render::GpuContext& ctx) {
    VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(SkinningPush)};
    VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &range;
    if (vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &skinningLayout_) != VK_SUCCESS) return false;
    skinningPipeline_ = render::createComputePipeline(ctx, skinningLayout_, "rt_skinning.comp.spv");
    return skinningPipeline_ != VK_NULL_HANDLE;
}

void RayTracingScene::shutdown() {
    if (!initialized_) return;
    vkQueueWaitIdle(queue_);
    for (auto& [uid, entry] : blasCache_) {
        vkDestroyAccelerationStructureKHR(device_, entry.blas, nullptr);
        destroyBuffer(entry.storage);
    }
    blasCache_.clear();
    if (skinningPipeline_ != VK_NULL_HANDLE) vkDestroyPipeline(device_, skinningPipeline_, nullptr);
    if (skinningLayout_ != VK_NULL_HANDLE) vkDestroyPipelineLayout(device_, skinningLayout_, nullptr);
    skinningPipeline_ = VK_NULL_HANDLE;
    skinningLayout_ = VK_NULL_HANDLE;
    if (cmdPool_ != VK_NULL_HANDLE) vkDestroyCommandPool(device_, cmdPool_, nullptr);
    cmdPool_ = VK_NULL_HANDLE;
    initialized_ = false;
}

RayTracingScene::Buffer RayTracingScene::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, bool hostVisible) {
    Buffer out;
    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = std::max<VkDeviceSize>(size, 16);
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    if (hostVisible) {
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    }
    VmaAllocationInfo info{};
    if (vmaCreateBuffer(allocator_, &bufferInfo, &allocInfo, &out.buffer, &out.allocation, &info) != VK_SUCCESS) {
        logError("RayTracingScene", "vmaCreateBuffer failed (size=%llu).", static_cast<unsigned long long>(size));
        return Buffer{};
    }
    out.mapped = info.pMappedData;
    out.size = bufferInfo.size;
    if ((usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) != 0) out.address = deviceAddress(out.buffer);
    return out;
}

void RayTracingScene::destroyBuffer(Buffer& buffer) {
    if (buffer.buffer != VK_NULL_HANDLE) vmaDestroyBuffer(allocator_, buffer.buffer, buffer.allocation);
    buffer = Buffer{};
}

bool RayTracingScene::ensureBuffer(Buffer& buffer, VkDeviceSize size, VkBufferUsageFlags usage, bool hostVisible) {
    if (buffer.buffer != VK_NULL_HANDLE && buffer.size >= size) return false;
    // Growth is rare (capacity doubles); waiting keeps in-flight users of the old buffer safe.
    if (buffer.buffer != VK_NULL_HANDLE) vkQueueWaitIdle(queue_);
    destroyBuffer(buffer);
    VkDeviceSize capacity = 4096;
    while (capacity < size) capacity *= 2;
    buffer = createBuffer(capacity, usage, hostVisible);
    return true;
}

VkDeviceAddress RayTracingScene::deviceAddress(VkBuffer buffer) const {
    VkBufferDeviceAddressInfo info{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
    info.buffer = buffer;
    return vkGetBufferDeviceAddress(device_, &info);
}

VkAccelerationStructureKHR RayTracingScene::createAccelerationStructure(VkAccelerationStructureTypeKHR type,
                                                                        const Buffer& storage,
                                                                        VkDeviceAddress& outAddress) {
    VkAccelerationStructureCreateInfoKHR createInfo{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
    createInfo.buffer = storage.buffer;
    createInfo.size = storage.size;
    createInfo.type = type;
    VkAccelerationStructureKHR structure = VK_NULL_HANDLE;
    if (vkCreateAccelerationStructureKHR(device_, &createInfo, nullptr, &structure) != VK_SUCCESS) {
        logError("RayTracingScene", "vkCreateAccelerationStructureKHR failed.");
        return VK_NULL_HANDLE;
    }
    VkAccelerationStructureDeviceAddressInfoKHR addressInfo{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
    addressInfo.accelerationStructure = structure;
    outAddress = vkGetAccelerationStructureDeviceAddressKHR(device_, &addressInfo);
    return structure;
}

void RayTracingScene::submitAndWait(const std::function<void(VkCommandBuffer)>& record) {
    VkCommandBufferAllocateInfo allocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocInfo.commandPool = cmdPool_;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(device_, &allocInfo, &cmd) != VK_SUCCESS) {
        logError("RayTracingScene", "vkAllocateCommandBuffers failed.");
        return;
    }
    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);
    record(cmd);
    vkEndCommandBuffer(cmd);
    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    vkQueueSubmit(queue_, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue_);
    vkFreeCommandBuffers(device_, cmdPool_, 1, &cmd);
}

void RayTracingScene::buildMissingBlases(const std::vector<RtMeshInstance>& meshes) {
    struct Pending {
        uint64_t uid;
        VkAccelerationStructureGeometryKHR geometry;
        VkAccelerationStructureBuildGeometryInfoKHR build;
        VkAccelerationStructureBuildRangeInfoKHR range;
        VkDeviceSize scratchOffset;
    };
    std::vector<Pending> pending;
    std::unordered_set<uint64_t> queued;
    VkDeviceSize scratchTotal = 0;

    for (const RtMeshInstance& instance : meshes) {
        const uint64_t uid = instance.mesh->uid();
        auto cached = blasCache_.find(uid);
        if (cached != blasCache_.end()) {
            cached->second.lastUsed = recordClock_;
            continue;
        }
        if (!queued.insert(uid).second) continue;

        Pending p{};
        p.uid = uid;
        p.geometry = triangleGeometry(deviceAddress(instance.mesh->vertexBuffer()), instance.mesh->vertexCount(),
                                      deviceAddress(instance.mesh->indexBuffer()));
        p.range.primitiveCount = instance.mesh->indexCount() / 3;
        p.build = VkAccelerationStructureBuildGeometryInfoKHR{
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
        p.build.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        p.build.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        p.build.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        p.build.geometryCount = 1;

        VkAccelerationStructureBuildGeometryInfoKHR sizeQuery = p.build;
        sizeQuery.pGeometries = &p.geometry;
        VkAccelerationStructureBuildSizesInfoKHR sizes{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
        vkGetAccelerationStructureBuildSizesKHR(device_, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &sizeQuery,
                                                &p.range.primitiveCount, &sizes);

        BlasEntry entry;
        entry.storage = createBuffer(sizes.accelerationStructureSize,
                                     VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, false);
        if (entry.storage.buffer == VK_NULL_HANDLE) continue;
        entry.blas = createAccelerationStructure(VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, entry.storage,
                                                 entry.address);
        if (entry.blas == VK_NULL_HANDLE) {
            destroyBuffer(entry.storage);
            continue;
        }
        entry.lastUsed = recordClock_;
        p.build.dstAccelerationStructure = entry.blas;
        p.scratchOffset = scratchTotal;
        scratchTotal += alignUp(sizes.buildScratchSize, scratchAlignment_);
        blasCache_.emplace(uid, entry);
        pending.push_back(p);
    }
    if (pending.empty()) return;

    Buffer scratch = createBuffer(scratchTotal + scratchAlignment_,
                                  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, false);
    if (scratch.buffer == VK_NULL_HANDLE) {
        for (const Pending& p : pending) {
            auto it = blasCache_.find(p.uid);
            vkDestroyAccelerationStructureKHR(device_, it->second.blas, nullptr);
            destroyBuffer(it->second.storage);
            blasCache_.erase(it);
        }
        return;
    }
    const VkDeviceAddress scratchBase = alignUp(scratch.address, scratchAlignment_);
    std::vector<VkAccelerationStructureBuildGeometryInfoKHR> builds;
    std::vector<const VkAccelerationStructureBuildRangeInfoKHR*> ranges;
    builds.reserve(pending.size());
    ranges.reserve(pending.size());
    for (Pending& p : pending) {
        p.build.pGeometries = &p.geometry;
        p.build.scratchData.deviceAddress = scratchBase + p.scratchOffset;
        builds.push_back(p.build);
        ranges.push_back(&p.range);
    }
    submitAndWait([&](VkCommandBuffer cmd) {
        vkCmdBuildAccelerationStructuresKHR(cmd, static_cast<uint32_t>(builds.size()), builds.data(), ranges.data());
    });
    destroyBuffer(scratch);
}

void RayTracingScene::releaseUnusedBlases() {
    bool waited = false;
    for (auto it = blasCache_.begin(); it != blasCache_.end();) {
        if (recordClock_ - it->second.lastUsed < kBlasIdleRecords) {
            ++it;
            continue;
        }
        if (!waited) {
            vkQueueWaitIdle(queue_);
            waited = true;
        }
        vkDestroyAccelerationStructureKHR(device_, it->second.blas, nullptr);
        destroyBuffer(it->second.storage);
        it = blasCache_.erase(it);
    }
}

void RayTracingScene::destroySkinnedBlas(SkinnedBlas& entry) {
    if (entry.blas != VK_NULL_HANDLE) vkDestroyAccelerationStructureKHR(device_, entry.blas, nullptr);
    destroyBuffer(entry.vertices);
    destroyBuffer(entry.storage);
    destroyBuffer(entry.scratch);
    entry = SkinnedBlas{};
}

bool RayTracingScene::initializeFrame(Frame& frame) {
    if (!initialized_) return false;
    if (frame.tlas != VK_NULL_HANDLE) return true;
    submitAndWait([&](VkCommandBuffer cmd) { record(cmd, frame, {}, {}); });
    return frame.tlas != VK_NULL_HANDLE;
}

void RayTracingScene::destroyFrame(Frame& frame) {
    if (!initialized_) return;
    vkQueueWaitIdle(queue_);
    if (frame.tlas != VK_NULL_HANDLE) vkDestroyAccelerationStructureKHR(device_, frame.tlas, nullptr);
    destroyBuffer(frame.tlasStorage);
    destroyBuffer(frame.tlasScratch);
    destroyBuffer(frame.instanceData);
    destroyBuffer(frame.hostUpload);
    for (auto& [key, entry] : frame.skinned) destroySkinnedBlas(entry);
    frame = Frame{};
}

void RayTracingScene::recordSkinnedBlases(VkCommandBuffer cmd, Frame& frame,
                                          const std::vector<RtSkinnedInstance>& skinned) {
    const uint32_t clock = frame.recordCount;
    std::vector<SkinnedBlas*> entries(skinned.size(), nullptr);
    for (size_t i = 0; i < skinned.size(); ++i) {
        const RtSkinnedInstance& instance = skinned[i];
        const Mesh& mesh = instance.mesh->mesh();
        SkinnedBlas& entry = frame.skinned[instance.key];
        if (entry.sourceUid != mesh.uid()) {
            if (entry.blas != VK_NULL_HANDLE) vkQueueWaitIdle(queue_);
            destroySkinnedBlas(entry);
            entry.vertices = createBuffer(sizeof(Vertex) * mesh.vertexCount(),
                                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                                              VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
                                          false);
            VkAccelerationStructureGeometryKHR geometry =
                triangleGeometry(entry.vertices.address, mesh.vertexCount(), deviceAddress(mesh.indexBuffer()));
            VkAccelerationStructureBuildGeometryInfoKHR build{
                VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
            build.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
            build.flags = VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR |
                          VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
            build.geometryCount = 1;
            build.pGeometries = &geometry;
            const uint32_t primitives = mesh.indexCount() / 3;
            VkAccelerationStructureBuildSizesInfoKHR sizes{
                VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
            vkGetAccelerationStructureBuildSizesKHR(device_, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build,
                                                    &primitives, &sizes);
            entry.storage = createBuffer(sizes.accelerationStructureSize,
                                         VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, false);
            entry.scratch = createBuffer(std::max(sizes.buildScratchSize, sizes.updateScratchSize) + scratchAlignment_,
                                         VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                         false);
            if (entry.vertices.buffer == VK_NULL_HANDLE || entry.storage.buffer == VK_NULL_HANDLE ||
                entry.scratch.buffer == VK_NULL_HANDLE) {
                destroySkinnedBlas(entry);
                continue;
            }
            entry.blas = createAccelerationStructure(VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, entry.storage,
                                                     entry.address);
            if (entry.blas == VK_NULL_HANDLE) {
                destroySkinnedBlas(entry);
                continue;
            }
            entry.sourceUid = mesh.uid();
        }
        entry.lastUsed = clock;
        entries[i] = &entry;
    }

    // Entities that left the scene; this frame's earlier use of them has completed.
    bool waited = false;
    for (auto it = frame.skinned.begin(); it != frame.skinned.end();) {
        if (it->second.lastUsed == clock && it->second.blas != VK_NULL_HANDLE) {
            ++it;
            continue;
        }
        if (!waited) {
            vkQueueWaitIdle(queue_);
            waited = true;
        }
        destroySkinnedBlas(it->second);
        it = frame.skinned.erase(it);
    }

    bool anySkinned = std::any_of(entries.begin(), entries.end(), [](SkinnedBlas* e) { return e != nullptr; });
    if (!anySkinned || skinningPipeline_ == VK_NULL_HANDLE) return;

    const VkDeviceSize paletteBase = frame.hostSegment * (frame.recordCount % kHostRing);
    VkDeviceSize paletteOffset = 0;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, skinningPipeline_);
    for (size_t i = 0; i < skinned.size(); ++i) {
        if (entries[i] == nullptr) continue;
        const RtSkinnedInstance& instance = skinned[i];
        const Mesh& mesh = instance.mesh->mesh();
        auto* palette = reinterpret_cast<glm::mat4*>(static_cast<char*>(frame.hostUpload.mapped) + paletteBase +
                                                     paletteOffset);
        std::memcpy(palette, instance.palette, sizeof(glm::mat4) * instance.jointCount);
        SkinningPush push{};
        push.source = splitAddress(deviceAddress(mesh.vertexBuffer()));
        push.skin = splitAddress(deviceAddress(instance.mesh->skinBuffer()));
        push.palette = splitAddress(frame.hostUpload.address + paletteBase + paletteOffset);
        push.destination = splitAddress(entries[i]->vertices.address);
        push.vertexCount = mesh.vertexCount();
        vkCmdPushConstants(cmd, skinningLayout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
        vkCmdDispatch(cmd, render::divideRoundUp(mesh.vertexCount(), kSkinningGroupSize), 1, 1);
        paletteOffset += alignUp(sizeof(glm::mat4) * instance.jointCount, 64);
    }
    memoryBarrier(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                  VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_ACCESS_2_SHADER_READ_BIT);

    std::vector<VkAccelerationStructureGeometryKHR> geometries(skinned.size());
    std::vector<VkAccelerationStructureBuildGeometryInfoKHR> builds;
    std::vector<VkAccelerationStructureBuildRangeInfoKHR> rangeStorage(skinned.size());
    std::vector<const VkAccelerationStructureBuildRangeInfoKHR*> ranges;
    for (size_t i = 0; i < skinned.size(); ++i) {
        SkinnedBlas* entry = entries[i];
        if (entry == nullptr) continue;
        const Mesh& mesh = skinned[i].mesh->mesh();
        geometries[i] = triangleGeometry(entry->vertices.address, mesh.vertexCount(), deviceAddress(mesh.indexBuffer()));
        VkAccelerationStructureBuildGeometryInfoKHR build{
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
        build.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        build.flags = VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR |
                      VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
        build.mode = entry->built ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR
                                  : VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        build.srcAccelerationStructure = entry->built ? entry->blas : VK_NULL_HANDLE;
        build.dstAccelerationStructure = entry->blas;
        build.geometryCount = 1;
        build.pGeometries = &geometries[i];
        build.scratchData.deviceAddress = alignUp(entry->scratch.address, scratchAlignment_);
        rangeStorage[i].primitiveCount = mesh.indexCount() / 3;
        builds.push_back(build);
        ranges.push_back(&rangeStorage[i]);
        entry->built = true;
    }
    vkCmdBuildAccelerationStructuresKHR(cmd, static_cast<uint32_t>(builds.size()), builds.data(), ranges.data());
    memoryBarrier(cmd, VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                  VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
                  VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                  VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR);
}

bool RayTracingScene::record(VkCommandBuffer cmd, Frame& frame, const std::vector<RtMeshInstance>& meshes,
                             const std::vector<RtSkinnedInstance>& skinned) {
    if (!initialized_) return false;
    ++recordClock_;
    ++frame.recordCount;
    buildMissingBlases(meshes);

    const size_t instanceCapacity = meshes.size() + skinned.size();
    size_t paletteJoints = 0;
    for (const RtSkinnedInstance& instance : skinned) paletteJoints += instance.jointCount + 4;
    const VkDeviceSize instanceBytes = alignUp(sizeof(VkAccelerationStructureInstanceKHR) * instanceCapacity + 16,
                                               kUploadAlignment);
    const VkDeviceSize dataBytes = alignUp(sizeof(RtInstanceData) * std::max<size_t>(instanceCapacity, 1),
                                           kUploadAlignment);
    const VkDeviceSize paletteBytes = alignUp(sizeof(glm::mat4) * paletteJoints, kUploadAlignment);
    const VkDeviceSize segment = instanceBytes + dataBytes + paletteBytes;

    bool descriptorsChanged = false;
    if (frame.hostUpload.buffer == VK_NULL_HANDLE || frame.hostSegment < segment) {
        VkDeviceSize newSegment = std::max<VkDeviceSize>(frame.hostSegment, 64 * 1024);
        while (newSegment < segment) newSegment *= 2;
        if (frame.hostUpload.buffer != VK_NULL_HANDLE) vkQueueWaitIdle(queue_);
        destroyBuffer(frame.hostUpload);
        frame.hostUpload = createBuffer(newSegment * kHostRing,
                                        VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                                            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
                                        true);
        frame.hostSegment = frame.hostUpload.buffer != VK_NULL_HANDLE ? newSegment : 0;
        if (frame.hostUpload.buffer == VK_NULL_HANDLE) return false;
    }
    descriptorsChanged |= ensureBuffer(frame.instanceData, dataBytes,
                                       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, false);
    if (frame.instanceData.buffer == VK_NULL_HANDLE) return false;

    // Earlier work that read this view's TLAS, BLASes or records must finish before they are rewritten.
    memoryBarrier(cmd, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                  VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR,
                  VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT |
                      VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                  VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT |
                      VK_ACCESS_2_TRANSFER_WRITE_BIT);

    const VkDeviceSize ringBase = frame.hostSegment * (frame.recordCount % kHostRing);
    auto* asInstances = reinterpret_cast<VkAccelerationStructureInstanceKHR*>(
        static_cast<char*>(frame.hostUpload.mapped) + ringBase + paletteBytes);
    auto* records = reinterpret_cast<RtInstanceData*>(static_cast<char*>(frame.hostUpload.mapped) + ringBase +
                                                      paletteBytes + instanceBytes);

    recordSkinnedBlases(cmd, frame, skinned);

    uint32_t count = 0;
    auto emit = [&](const glm::mat4& transform, uint8_t mask, VkDeviceAddress blas, RtInstanceData data) {
        VkAccelerationStructureInstanceKHR& out = asInstances[count];
        out = VkAccelerationStructureInstanceKHR{};
        out.transform = toVkTransform(transform);
        out.instanceCustomIndex = count;
        out.mask = mask;
        out.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        out.accelerationStructureReference = blas;
        records[count] = data;
        ++count;
    };
    for (const RtMeshInstance& instance : meshes) {
        auto it = blasCache_.find(instance.mesh->uid());
        if (it == blasCache_.end()) continue;
        RtInstanceData data = instance.material;
        data.geometry = glm::uvec4(splitAddress(deviceAddress(instance.mesh->vertexBuffer())),
                                   splitAddress(deviceAddress(instance.mesh->indexBuffer())));
        emit(instance.transform, instance.mask, it->second.address, data);
    }
    for (const RtSkinnedInstance& instance : skinned) {
        auto it = frame.skinned.find(instance.key);
        if (it == frame.skinned.end() || !it->second.built || skinningPipeline_ == VK_NULL_HANDLE) continue;
        RtInstanceData data = instance.material;
        data.geometry = glm::uvec4(splitAddress(it->second.vertices.address),
                                   splitAddress(deviceAddress(instance.mesh->mesh().indexBuffer())));
        emit(instance.transform, instance.mask, it->second.address, data);
    }
    frame.instanceCount = count;

    if (count > 0) {
        VkBufferCopy copy{ringBase + paletteBytes + instanceBytes, 0, sizeof(RtInstanceData) * count};
        vkCmdCopyBuffer(cmd, frame.hostUpload.buffer, frame.instanceData.buffer, 1, &copy);
    }

    VkAccelerationStructureGeometryKHR geometry{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
    geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    geometry.geometry.instances.data.deviceAddress = frame.hostUpload.address + ringBase + paletteBytes;
    VkAccelerationStructureBuildGeometryInfoKHR build{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
    build.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    build.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    build.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    build.geometryCount = 1;
    build.pGeometries = &geometry;
    VkAccelerationStructureBuildSizesInfoKHR sizes{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    vkGetAccelerationStructureBuildSizesKHR(device_, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build, &count,
                                            &sizes);

    if (ensureBuffer(frame.tlasStorage, sizes.accelerationStructureSize,
                     VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, false) ||
        frame.tlas == VK_NULL_HANDLE) {
        if (frame.tlas != VK_NULL_HANDLE) vkDestroyAccelerationStructureKHR(device_, frame.tlas, nullptr);
        VkDeviceAddress unused = 0;
        frame.tlas = frame.tlasStorage.buffer != VK_NULL_HANDLE
                         ? createAccelerationStructure(VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, frame.tlasStorage,
                                                       unused)
                         : VK_NULL_HANDLE;
        descriptorsChanged = true;
    }
    ensureBuffer(frame.tlasScratch, sizes.buildScratchSize + scratchAlignment_,
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, false);
    if (frame.tlas == VK_NULL_HANDLE || frame.tlasScratch.buffer == VK_NULL_HANDLE) return descriptorsChanged;

    build.dstAccelerationStructure = frame.tlas;
    build.scratchData.deviceAddress = alignUp(frame.tlasScratch.address, scratchAlignment_);
    VkAccelerationStructureBuildRangeInfoKHR range{};
    range.primitiveCount = count;
    const VkAccelerationStructureBuildRangeInfoKHR* rangePtr = &range;
    vkCmdBuildAccelerationStructuresKHR(cmd, 1, &build, &rangePtr);

    memoryBarrier(cmd, VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                  VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR | VK_ACCESS_2_TRANSFER_WRITE_BIT,
                  VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                  VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_2_SHADER_READ_BIT);

    if (recordClock_ % 64 == 0) releaseUnusedBlases();
    return descriptorsChanged;
}

} // namespace engine::core
