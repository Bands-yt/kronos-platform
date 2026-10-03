#pragma once

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>
#include <volk.h>
#include <vk_mem_alloc.h>

#include "core/Mesh.hpp"
#include "core/render/GpuHelpers.hpp"

namespace engine::core {

class RiggedMesh;

// Surface data ray-traced hit shading reads per TLAS instance. std430;
// mirrors RtInstance in shaders/kronos/raytracing.glsl.
struct RtInstanceData {
    glm::vec4 baseColor{1.0f};
    glm::vec4 surface{0.0f, 1.0f, 0.0f, 0.0f}; // x metallic, y perceptual roughness
    glm::vec4 emissive{0.0f};                   // rgb radiance
    glm::uvec4 geometry{0u};                    // xy vertex buffer address, zw index buffer address
    glm::uvec4 textures{0u};                    // x albedo bindless slot (0 = white)
};
static_assert(sizeof(RtInstanceData) == 80);

[[nodiscard]] RtInstanceData makeRtMaterial(glm::vec4 baseColor, float metallic, float roughness, glm::vec3 emissiveColor,
                                            float emissiveIntensity, uint32_t albedoSlot);

// Instance mask bits: shadow rays only see casters, every other ray sees everything visible.
inline constexpr uint8_t kRtMaskShadowCaster = 0x01;
inline constexpr uint8_t kRtMaskVisible = 0x02;

[[nodiscard]] constexpr uint8_t rtInstanceMask(bool castsShadow) {
    return castsShadow ? uint8_t(kRtMaskShadowCaster | kRtMaskVisible) : kRtMaskVisible;
}

struct RtMeshInstance {
    const Mesh* mesh = nullptr;
    glm::mat4 transform{1.0f};
    uint8_t mask = rtInstanceMask(true);
    RtInstanceData material;
};

struct RtSkinnedInstance {
    const RiggedMesh* mesh = nullptr;
    uint64_t key = 0; // stable per entity so its BLAS is refit, not rebuilt
    glm::mat4 transform{1.0f};
    const glm::mat4* palette = nullptr;
    uint32_t jointCount = 0;
    uint8_t mask = rtInstanceMask(true);
    RtInstanceData material;
};

// Hardware ray tracing scene: one BLAS per uploaded Mesh (cached by uid),
// GPU-skinned BLASes refit every frame for animated characters, and a TLAS
// per in-flight frame rebuilt inside that frame's command buffer.
class RayTracingScene {
public:
    struct Buffer {
        VkBuffer buffer = VK_NULL_HANDLE;
        VmaAllocation allocation = nullptr;
        VkDeviceAddress address = 0;
        void* mapped = nullptr;
        VkDeviceSize size = 0;
    };

    struct SkinnedBlas {
        Buffer vertices;
        Buffer storage;
        Buffer scratch;
        VkAccelerationStructureKHR blas = VK_NULL_HANDLE;
        VkDeviceAddress address = 0;
        uint64_t sourceUid = 0;
        uint32_t lastUsed = 0;
        bool built = false;
    };

    // Host-written data (TLAS instances, hit-shading records, bone palettes)
    // cycles through this many segments: an auxiliary scene is recorded into
    // consecutive frames' command buffers, so its previous upload may still
    // be in flight when the next one is written.
    static constexpr uint32_t kHostRing = 3;

    // Everything one view traces against. Owned by that view's frame
    // resources; GPU-side reuse is ordered by barriers in record().
    struct Frame {
        VkAccelerationStructureKHR tlas = VK_NULL_HANDLE;
        Buffer tlasStorage;
        Buffer tlasScratch;
        Buffer instanceData; // device-local, bound at set 0 binding 3
        Buffer hostUpload;   // kHostRing segments
        VkDeviceSize hostSegment = 0;
        std::unordered_map<uint64_t, SkinnedBlas> skinned;
        uint32_t recordCount = 0;
        uint32_t instanceCount = 0;
    };

    ~RayTracingScene();

    [[nodiscard]] bool initialize(VmaAllocator allocator, VkDevice device, VkPhysicalDevice physicalDevice,
                                  uint32_t queueFamilyIndex, VkQueue queue);
    // Needs the pipeline cache and shader directory, which exist later than the device.
    [[nodiscard]] bool initializeSkinning(const render::GpuContext& ctx);
    void shutdown();

    // Gives `frame` a valid empty TLAS so its descriptors can be written immediately.
    [[nodiscard]] bool initializeFrame(Frame& frame);
    void destroyFrame(Frame& frame);

    // Records skinning, BLAS builds/refits and the TLAS build into `cmd`
    // (outside any render pass), ending in a barrier that makes the TLAS
    // visible to shader ray queries. Returns true when the frame's TLAS or
    // instance-data buffer changed handle, so its descriptors need rewriting.
    bool record(VkCommandBuffer cmd, Frame& frame, const std::vector<RtMeshInstance>& meshes,
                const std::vector<RtSkinnedInstance>& skinned);

    [[nodiscard]] bool initialized() const { return initialized_; }
    [[nodiscard]] size_t cachedBlasCount() const { return blasCache_.size(); }

    [[nodiscard]] static VkTransformMatrixKHR toVkTransform(const glm::mat4& m);

private:
    struct BlasEntry {
        VkAccelerationStructureKHR blas = VK_NULL_HANDLE;
        Buffer storage;
        VkDeviceAddress address = 0;
        uint32_t lastUsed = 0;
    };

    [[nodiscard]] Buffer createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, bool hostVisible);
    void destroyBuffer(Buffer& buffer);
    // Grow-only; returns true if the buffer was (re)allocated.
    bool ensureBuffer(Buffer& buffer, VkDeviceSize size, VkBufferUsageFlags usage, bool hostVisible);
    [[nodiscard]] VkDeviceAddress deviceAddress(VkBuffer buffer) const;
    [[nodiscard]] VkAccelerationStructureKHR createAccelerationStructure(VkAccelerationStructureTypeKHR type,
                                                                        const Buffer& storage,
                                                                        VkDeviceAddress& outAddress);
    void buildMissingBlases(const std::vector<RtMeshInstance>& meshes);
    void recordSkinnedBlases(VkCommandBuffer cmd, Frame& frame, const std::vector<RtSkinnedInstance>& skinned);
    void releaseUnusedBlases();
    void destroySkinnedBlas(SkinnedBlas& entry);
    void submitAndWait(const std::function<void(VkCommandBuffer)>& record);

    VmaAllocator allocator_ = nullptr;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    VkCommandPool cmdPool_ = VK_NULL_HANDLE;
    VkDeviceSize scratchAlignment_ = 256;

    VkPipelineLayout skinningLayout_ = VK_NULL_HANDLE;
    VkPipeline skinningPipeline_ = VK_NULL_HANDLE;

    std::unordered_map<uint64_t, BlasEntry> blasCache_;
    uint32_t recordClock_ = 0;
    bool initialized_ = false;
};

} // namespace engine::core
