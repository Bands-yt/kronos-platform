#include "core/RayTracingScene.hpp"

#include <cstring>

#include <glm/gtc/matrix_transform.hpp>
#include "core/Logger.hpp"

namespace engine::core {

namespace {

// Real position-only geometry, matching core::Mesh::createBox()/createPlane()'s
// exact vertex positions and winding (Mesh.cpp) so an RT shadow silhouette
// lines up with the rasterized mesh it stands in for. BLAS geometry only
// needs positions -- no normal/uv/tangent, unlike core::Mesh::Vertex.
struct PositionMesh {
    std::vector<glm::vec3> positions;
    std::vector<uint32_t> indices;
};

PositionMesh buildBoxPositions(glm::vec3 h) {
    PositionMesh mesh;
    mesh.positions = {
        // +X
        {h.x, -h.y, -h.z}, {h.x, -h.y, h.z}, {h.x, h.y, h.z}, {h.x, h.y, -h.z},
        // -X
        {-h.x, -h.y, h.z}, {-h.x, -h.y, -h.z}, {-h.x, h.y, -h.z}, {-h.x, h.y, h.z},
        // +Y
        {-h.x, h.y, -h.z}, {h.x, h.y, -h.z}, {h.x, h.y, h.z}, {-h.x, h.y, h.z},
        // -Y
        {-h.x, -h.y, h.z}, {h.x, -h.y, h.z}, {h.x, -h.y, -h.z}, {-h.x, -h.y, -h.z},
        // +Z
        {h.x, -h.y, h.z}, {-h.x, -h.y, h.z}, {-h.x, h.y, h.z}, {h.x, h.y, h.z},
        // -Z
        {-h.x, -h.y, -h.z}, {h.x, -h.y, -h.z}, {h.x, h.y, -h.z}, {-h.x, h.y, -h.z},
    };
    mesh.indices.reserve(36);
    for (uint32_t face = 0; face < 6; ++face) {
        uint32_t base = face * 4;
        mesh.indices.insert(mesh.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }
    return mesh;
}

PositionMesh buildPlanePositions(float halfWidth, float halfDepth) {
    PositionMesh mesh;
    mesh.positions = {
        {-halfWidth, 0.0f, -halfDepth}, {halfWidth, 0.0f, -halfDepth}, {halfWidth, 0.0f, halfDepth}, {-halfWidth, 0.0f, halfDepth}};
    mesh.indices = {0, 1, 2, 0, 2, 3};
    return mesh;
}

} // namespace

bool RayTracingScene::ShapeKey::operator==(const ShapeKey& other) const {
    return kind == other.kind && params == other.params;
}

size_t RayTracingScene::ShapeKeyHash::operator()(const ShapeKey& key) const {
    size_t h = std::hash<int>()(static_cast<int>(key.kind));
    h ^= std::hash<float>()(key.params.x) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<float>()(key.params.y) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<float>()(key.params.z) + 0x9e3779b9 + (h << 6) + (h >> 2);
    return h;
}

RayTracingScene::~RayTracingScene() { shutdown(); }

bool RayTracingScene::initialize(VmaAllocator allocator, VkDevice device, VkPhysicalDevice physicalDevice,
                                  uint32_t queueFamilyIndex, VkQueue queue) {
    allocator_ = allocator;
    device_ = device;
    physicalDevice_ = physicalDevice;
    queue_ = queue;

    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT | VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    poolInfo.queueFamilyIndex = queueFamilyIndex;
    if (vkCreateCommandPool(device_, &poolInfo, nullptr, &cmdPool_) != VK_SUCCESS) {
        logError("RayTracingScene", "vkCreateCommandPool failed.");
        return false;
    }

    initialized_ = true;

    // Real, valid (empty) TLAS from the moment initialize() succeeds --
    // see rebuild()'s own comment on why this matters (the shader-side
    // descriptor binding must always point at something real and valid).
    rebuild({});
    return tlas_ != VK_NULL_HANDLE;
}

void RayTracingScene::shutdown() {
    if (!initialized_) return;

    destroyTlas();
    destroyBuffer(tlasScratch_);
    destroyBuffer(tlasInstanceBuffer_);
    tlasScratchCapacity_ = 0;
    tlasInstanceCapacity_ = 0;
    destroyBuffer(materialsBuffer_);
    materialsBufferCapacity_ = 0;
    materials_.clear();

    for (auto& [key, entry] : blasCache_) {
        if (entry.blas != VK_NULL_HANDLE) vkDestroyAccelerationStructureKHR(device_, entry.blas, nullptr);
        destroyBuffer(entry.asBuffer);
        destroyBuffer(entry.vertexBuffer);
        destroyBuffer(entry.indexBuffer);
    }
    blasCache_.clear();

    if (cmdPool_ != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device_, cmdPool_, nullptr);
        cmdPool_ = VK_NULL_HANDLE;
    }
    initialized_ = false;
}

RayTracingScene::Buffer RayTracingScene::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                                        VmaMemoryUsage memUsage, VmaAllocationCreateFlags flags,
                                                        bool wantAddress) {
    Buffer out;
    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = memUsage;
    allocInfo.flags = flags;

    if (vmaCreateBuffer(allocator_, &bufferInfo, &allocInfo, &out.buffer, &out.allocation, nullptr) != VK_SUCCESS) {
        logError("RayTracingScene", "vmaCreateBuffer failed (size=%llu).", static_cast<unsigned long long>(size));
        return out;
    }
    if (wantAddress) out.address = bufferDeviceAddress(out.buffer);
    return out;
}

void RayTracingScene::destroyBuffer(Buffer& buffer) {
    if (buffer.buffer != VK_NULL_HANDLE) vmaDestroyBuffer(allocator_, buffer.buffer, buffer.allocation);
    buffer = Buffer{};
}

VkDeviceAddress RayTracingScene::bufferDeviceAddress(VkBuffer buffer) const {
    VkBufferDeviceAddressInfo info{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
    info.buffer = buffer;
    return vkGetBufferDeviceAddress(device_, &info);
}

void RayTracingScene::submitAndWait(const std::function<void(VkCommandBuffer)>& record) {
    VkCommandBufferAllocateInfo allocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocInfo.commandPool = cmdPool_;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    // Kronos (Phase 1 stability audit fix): real result check -- see
    // core::uploadToDeviceLocalBuffer() in Mesh.cpp for the identical
    // real bug this mirrors. This function is void (the `record` lambda
    // has no way to report failure back to its caller), so the real,
    // honest response to allocation failure is to log and skip the
    // record/submit entirely rather than dereference a null cmd.
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

RayTracingScene::BlasEntry RayTracingScene::buildBlasFor(const ShapeKey& key) {
    BlasEntry entry;

    PositionMesh geometry;
    switch (key.kind) {
        case MeshSourceKind::Box: geometry = buildBoxPositions(key.params); break;
        case MeshSourceKind::Plane: geometry = buildPlanePositions(key.params.x, key.params.z); break;
        default:
            // Real, honest unsupported-shape case -- Capsule/Quad/Obj/
            // Gltf don't participate in ray-traced shadows this pass, see
            // this class's own header comment. Empty entry (blas ==
            // VK_NULL_HANDLE) signals "skip this instance" to the caller.
            return entry;
    }

    VkDeviceSize vertexBytes = sizeof(glm::vec3) * geometry.positions.size();
    VkDeviceSize indexBytes = sizeof(uint32_t) * geometry.indices.size();

    Buffer vertexStaging = createBuffer(vertexBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_AUTO,
                                         VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
                                         false);
    VmaAllocationInfo vertexStagingInfo{};
    vmaGetAllocationInfo(allocator_, vertexStaging.allocation, &vertexStagingInfo);
    std::memcpy(vertexStagingInfo.pMappedData, geometry.positions.data(), static_cast<size_t>(vertexBytes));

    Buffer indexStaging = createBuffer(indexBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_AUTO,
                                        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
                                        false);
    VmaAllocationInfo indexStagingInfo{};
    vmaGetAllocationInfo(allocator_, indexStaging.allocation, &indexStagingInfo);
    std::memcpy(indexStagingInfo.pMappedData, geometry.indices.data(), static_cast<size_t>(indexBytes));

    VkBufferUsageFlags asInputUsage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                                       VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    entry.vertexBuffer = createBuffer(vertexBytes, asInputUsage, VMA_MEMORY_USAGE_AUTO, 0, true);
    entry.indexBuffer = createBuffer(indexBytes, asInputUsage, VMA_MEMORY_USAGE_AUTO, 0, true);

    submitAndWait([&](VkCommandBuffer cmd) {
        VkBufferCopy vertexCopy{0, 0, vertexBytes};
        vkCmdCopyBuffer(cmd, vertexStaging.buffer, entry.vertexBuffer.buffer, 1, &vertexCopy);
        VkBufferCopy indexCopy{0, 0, indexBytes};
        vkCmdCopyBuffer(cmd, indexStaging.buffer, entry.indexBuffer.buffer, 1, &indexCopy);
    });
    destroyBuffer(vertexStaging);
    destroyBuffer(indexStaging);

    VkAccelerationStructureGeometryTrianglesDataKHR triData{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR};
    triData.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    triData.vertexData.deviceAddress = entry.vertexBuffer.address;
    triData.vertexStride = sizeof(glm::vec3);
    triData.maxVertex = static_cast<uint32_t>(geometry.positions.size() - 1);
    triData.indexType = VK_INDEX_TYPE_UINT32;
    triData.indexData.deviceAddress = entry.indexBuffer.address;

    VkAccelerationStructureGeometryKHR asGeometry{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
    asGeometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    asGeometry.geometry.triangles = triData;
    asGeometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;

    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR; // built once, cached, traced every frame -- optimize for trace speed
    buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries = &asGeometry;

    uint32_t primitiveCount = static_cast<uint32_t>(geometry.indices.size() / 3);
    VkAccelerationStructureBuildSizesInfoKHR sizeInfo{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    vkGetAccelerationStructureBuildSizesKHR(device_, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo,
                                             &primitiveCount, &sizeInfo);

    entry.asBuffer = createBuffer(sizeInfo.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,
                                   VMA_MEMORY_USAGE_AUTO, 0, false);

    VkAccelerationStructureCreateInfoKHR createInfo{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
    createInfo.buffer = entry.asBuffer.buffer;
    createInfo.size = sizeInfo.accelerationStructureSize;
    createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    if (vkCreateAccelerationStructureKHR(device_, &createInfo, nullptr, &entry.blas) != VK_SUCCESS) {
        logError("RayTracingScene", "vkCreateAccelerationStructureKHR (BLAS) failed.");
        destroyBuffer(entry.asBuffer);
        destroyBuffer(entry.vertexBuffer);
        destroyBuffer(entry.indexBuffer);
        return BlasEntry{};
    }

    Buffer scratch = createBuffer(sizeInfo.buildScratchSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                   VMA_MEMORY_USAGE_AUTO, 0, true);

    buildInfo.dstAccelerationStructure = entry.blas;
    buildInfo.scratchData.deviceAddress = scratch.address;

    VkAccelerationStructureBuildRangeInfoKHR range{};
    range.primitiveCount = primitiveCount;
    const VkAccelerationStructureBuildRangeInfoKHR* pRange = &range;

    submitAndWait([&](VkCommandBuffer cmd) { vkCmdBuildAccelerationStructuresKHR(cmd, 1, &buildInfo, &pRange); });
    destroyBuffer(scratch);

    VkAccelerationStructureDeviceAddressInfoKHR addrInfo{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
    addrInfo.accelerationStructure = entry.blas;
    entry.blasAddress = vkGetAccelerationStructureDeviceAddressKHR(device_, &addrInfo);
    return entry;
}

const RayTracingScene::BlasEntry* RayTracingScene::getOrBuildBlas(const ShapeKey& key) {
    auto it = blasCache_.find(key);
    if (it != blasCache_.end()) return &it->second;

    BlasEntry entry = buildBlasFor(key);
    if (entry.blas == VK_NULL_HANDLE) return nullptr; // unsupported shape or a real build failure
    auto [inserted, ok] = blasCache_.emplace(key, entry);
    (void)ok;
    return &inserted->second;
}

void RayTracingScene::destroyTlas() {
    if (tlas_ != VK_NULL_HANDLE) {
        vkDestroyAccelerationStructureKHR(device_, tlas_, nullptr);
        tlas_ = VK_NULL_HANDLE;
    }
    destroyBuffer(tlasBuffer_);
    tlasBufferCapacity_ = 0;
}

bool RayTracingScene::isSupportedShapeKind(MeshSourceKind kind) {
    // Real, exact mirror of buildBlasFor()'s own switch -- kept as its
    // own tiny, pure, device-free function specifically so that filter's
    // *shape* (which MeshSourceKind values survive) is directly testable
    // without a live Vulkan device.
    switch (kind) {
        case MeshSourceKind::Box:
        case MeshSourceKind::Plane:
            return true;
        default:
            return false;
    }
}

std::vector<glm::vec4> RayTracingScene::packMaterials(const std::vector<Instance>& survivingInstances) {
    std::vector<glm::vec4> packed;
    packed.reserve(survivingInstances.size() * 2);
    for (const Instance& inst : survivingInstances) {
        packed.push_back(inst.baseColor);
        packed.push_back(glm::vec4(inst.metallic, inst.roughness, 0.0f, 0.0f));
    }
    return packed;
}

void RayTracingScene::rebuild(const std::vector<Instance>& instances) {
    // Real, honest no-op when a real TLAS already exists from a previous
    // call and there's nothing new to build -- keeps the existing one
    // bound rather than tearing it down for an empty frame. The *first*
    // call (tlas_ still null -- e.g. right after initialize(), before any
    // real shadow-casting entities exist) falls through instead and
    // builds a real, valid, zero-primitive TLAS: scene.frag's descriptor
    // binding must always point at *something* real and valid once this
    // pipeline variant is in use, even before the scene has any real
    // ray-traceable content.
    if (instances.empty() && tlas_ != VK_NULL_HANDLE) return;

    std::vector<VkAccelerationStructureInstanceKHR> asInstances;
    asInstances.reserve(instances.size());
    // Kronos ("Rendering Fidelity Foundation" Phase 1.3): collected in
    // lockstep with asInstances below (same loop, same iteration, same
    // `continue` on an unsupported shape) -- this is what guarantees
    // survivingInstances[i]'s own material really is the material for
    // whatever geometry asInstances[i] (instanceCustomIndex == i, set
    // below) actually references, by construction, rather than by two
    // independently-filtered lists happening to agree.
    std::vector<Instance> survivingInstances;
    survivingInstances.reserve(instances.size());
    for (const Instance& inst : instances) {
        ShapeKey key{inst.kind, inst.params};
        const BlasEntry* blas = getOrBuildBlas(key);
        if (!blas) continue; // real, honest skip -- unsupported shape (Capsule/Quad/Obj) this pass

        VkAccelerationStructureInstanceKHR asInstance{};
        // Real row-major 3x4 transform matrix from glm's column-major mat4.
        glm::mat4 transposed = glm::transpose(inst.transform);
        for (int row = 0; row < 3; ++row) {
            for (int col = 0; col < 4; ++col) {
                asInstance.transform.matrix[row][col] = transposed[row][col];
            }
        }
        asInstance.mask = 0xFF;
        asInstance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        asInstance.accelerationStructureReference = blas->blasAddress;
        // Real index into materialsBuffer() (2 vec4 slots per instance --
        // see scene_rt.frag's own traceReflection()): assigned as this
        // survivor's own position in survivingInstances, *before* it's
        // pushed below, so it's a real, valid 0-based index into whatever
        // packMaterials(survivingInstances) produces from that same vector.
        asInstance.instanceCustomIndex = static_cast<uint32_t>(survivingInstances.size());
        survivingInstances.push_back(inst);
        asInstances.push_back(asInstance);
    }
    if (asInstances.empty() && tlas_ != VK_NULL_HANDLE) return; // every instance this call was an unsupported shape -- keep the existing real TLAS

    materials_ = packMaterials(survivingInstances);
    VkDeviceSize materialsBytes = sizeof(glm::vec4) * std::max<size_t>(materials_.size(), 1);
    if (materialsBytes > materialsBufferCapacity_) {
        destroyBuffer(materialsBuffer_);
        materialsBuffer_ = createBuffer(materialsBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_AUTO,
                                         VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
                                         false);
        materialsBufferCapacity_ = materialsBytes;
    }
    VmaAllocationInfo materialsInfo{};
    vmaGetAllocationInfo(allocator_, materialsBuffer_.allocation, &materialsInfo);
    if (!materials_.empty()) {
        std::memcpy(materialsInfo.pMappedData, materials_.data(), sizeof(glm::vec4) * materials_.size());
    }

    // Real minimum-size instance buffer even for a real zero-primitive
    // build -- VkAccelerationStructureGeometryInstancesDataKHR::data
    // still needs a real, valid device address to populate (the driver
    // simply won't read past 0 real instances), and this keeps the
    // "ensure a buffer of at least the needed size exists" logic below
    // uniform for both the zero- and non-zero-instance cases.
    VkDeviceSize instanceBytes = sizeof(VkAccelerationStructureInstanceKHR) * std::max<size_t>(asInstances.size(), 1);
    if (instanceBytes > tlasInstanceCapacity_) {
        destroyBuffer(tlasInstanceBuffer_);
        tlasInstanceBuffer_ =
            createBuffer(instanceBytes, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                         VMA_MEMORY_USAGE_AUTO, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT, true);
        tlasInstanceCapacity_ = instanceBytes;
    }
    VmaAllocationInfo instanceInfo{};
    vmaGetAllocationInfo(allocator_, tlasInstanceBuffer_.allocation, &instanceInfo);
    // Real, actual instance count's worth of bytes -- deliberately not
    // `instanceBytes` above (which may be padded to a real minimum-1 for
    // the zero-instance buffer-sizing case): copying that padded size
    // here would read past `asInstances`' real, possibly-empty backing
    // storage.
    if (!asInstances.empty()) {
        std::memcpy(instanceInfo.pMappedData, asInstances.data(), sizeof(VkAccelerationStructureInstanceKHR) * asInstances.size());
    }

    VkAccelerationStructureGeometryInstancesDataKHR instancesData{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR};
    instancesData.arrayOfPointers = VK_FALSE;
    instancesData.data.deviceAddress = tlasInstanceBuffer_.address;

    VkAccelerationStructureGeometryKHR geometry{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
    geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geometry.geometry.instances = instancesData;

    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    // Real-rebuilt every frame (a fresh camera/entity-transform set each
    // frame, see rebuild()'s own header comment) -- optimize for real
    // fast build, not fast trace, matching Sprint 14's own "stable
    // 180fps" budget concern: a TLAS is cheap to trace against relative
    // to a BLAS regardless of this flag.
    buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
    buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries = &geometry;

    uint32_t primitiveCount = static_cast<uint32_t>(asInstances.size());
    VkAccelerationStructureBuildSizesInfoKHR sizeInfo{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    vkGetAccelerationStructureBuildSizesKHR(device_, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo,
                                             &primitiveCount, &sizeInfo);

    // Real grow-only TLAS storage buffer + AS handle -- only destroy/
    // recreate when the real required size exceeds what's already
    // allocated (tracked via tlasBufferCapacity_, since VMA doesn't
    // expose a buffer's originally-requested size back cheaply). Typical
    // frame-to-frame scenes have a stable instance count, so this is the
    // common "reuse the same TLAS object, just re-record the build
    // command against it" fast path, not a reallocation every frame.
    if (sizeInfo.accelerationStructureSize > tlasBufferCapacity_) {
        destroyTlas();
        tlasBuffer_ = createBuffer(sizeInfo.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,
                                    VMA_MEMORY_USAGE_AUTO, 0, false);
        tlasBufferCapacity_ = sizeInfo.accelerationStructureSize;
        VkAccelerationStructureCreateInfoKHR createInfo{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
        createInfo.buffer = tlasBuffer_.buffer;
        createInfo.size = sizeInfo.accelerationStructureSize;
        createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        if (vkCreateAccelerationStructureKHR(device_, &createInfo, nullptr, &tlas_) != VK_SUCCESS) {
            logError("RayTracingScene", "vkCreateAccelerationStructureKHR (TLAS) failed.");
            destroyTlas();
            return;
        }
    }

    if (sizeInfo.buildScratchSize > tlasScratchCapacity_) {
        destroyBuffer(tlasScratch_);
        tlasScratch_ = createBuffer(sizeInfo.buildScratchSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                     VMA_MEMORY_USAGE_AUTO, 0, true);
        tlasScratchCapacity_ = sizeInfo.buildScratchSize;
    }

    buildInfo.dstAccelerationStructure = tlas_;
    buildInfo.scratchData.deviceAddress = tlasScratch_.address;

    VkAccelerationStructureBuildRangeInfoKHR range{};
    range.primitiveCount = primitiveCount;
    const VkAccelerationStructureBuildRangeInfoKHR* pRange = &range;

    submitAndWait([&](VkCommandBuffer cmd) { vkCmdBuildAccelerationStructuresKHR(cmd, 1, &buildInfo, &pRange); });
}

} // namespace engine::core
