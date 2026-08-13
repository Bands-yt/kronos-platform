#pragma once

#include <functional>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>
#include <volk.h>
#include <vk_mem_alloc.h>

#include "core/Components.hpp"

namespace engine::core {

// Sprint 14 ("RTX Upgrade" Phase 2)'s real hardware acceleration
// structure scene for ray-traced shadows via VK_KHR_ray_query (inline
// ray tracing invoked directly from scene.frag -- no separate ray
// tracing pipeline, no raygen/miss/closest-hit shaders, no shader
// binding table needed for a pure visibility test like a shadow ray).
// Builds one real BLAS per distinct (MeshSourceKind, params) shape,
// cached and only rebuilt the first time a given shape is seen, and one
// real TLAS every frame instancing every live shadow-casting entity's
// BLAS at its current real world transform.
//
// Deliberately scoped to MeshSourceKind::Box/Plane only -- the two
// shapes this engine's procedural content (Studio's Prefab/
// TntWarsPlugin/TerrainEditor's non-terrain props, engine_runtime's
// bring-up scene) is actually authored from. core::Mesh's own GPU
// vertex/index buffers are deliberately NOT reused here: they're created
// without VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT/
// VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
// (see Mesh.cpp), and adding those usage bits unconditionally to that
// shared, pervasively-used class would be invalid Vulkan usage on any
// future device that doesn't have bufferDeviceAddress enabled -- with no
// validation layer installed in this environment to catch that
// immediately, retrofitting Mesh for every existing call site across the
// whole engine was the wrong risk to take for this pass. Instead this
// class regenerates real, position-only geometry directly from the same
// real MeshSourceKind/params data scene serialization already uses (see
// Components.hpp's own MeshSource comment on why that data exists),
// into its own dedicated, real RT-flagged buffers. Imported OBJ meshes
// and core::Terrain chunks (which don't carry MeshSource params in this
// shape) simply don't participate -- they keep casting the existing,
// unchanged real CSM rasterized shadow. A real, stated scope boundary,
// not a silently-dropped one.
class RayTracingScene {
public:
    ~RayTracingScene();

    // Real, one-time setup against an already-ray-tracing-capable device
    // (see Renderer::checkRayTracingSupport()) -- creates the one real
    // command pool/buffer this class reuses every frame to record and
    // submit its own real BLAS/TLAS build commands.
    [[nodiscard]] bool initialize(VmaAllocator allocator, VkDevice device, VkPhysicalDevice physicalDevice,
                                   uint32_t queueFamilyIndex, VkQueue queue);
    void shutdown();

    struct Instance {
        MeshSourceKind kind = MeshSourceKind::Box;
        glm::vec3 params{0.5f};
        glm::mat4 transform{1.0f};
        // Kronos ("Rendering Fidelity Foundation" Phase 1.3): real PBR
        // material data carried alongside this instance's existing
        // position-only geometry -- shadows never needed to know *what*
        // was hit, only *whether* something was (a boolean ray query),
        // but hybrid RT reflections need the hit surface's own real
        // color/metallic/roughness to shade a reflection ray's result.
        // See packMaterials()'s own comment for how this reaches the GPU.
        glm::vec4 baseColor{1.0f};
        float metallic = 0.0f;
        float roughness = 1.0f;
    };

    // Pure, device-free proxy for buildBlasFor()'s own real switch (Box/
    // Plane build a real BLAS, every other kind returns an empty entry
    // unconditionally, before touching any device resource -- see that
    // function's own comment) -- lets that real filter's *shape* be
    // tested without a live Vulkan device. Public and static.
    [[nodiscard]] static bool isSupportedShapeKind(MeshSourceKind kind);

    // Pure -- packs 2 vec4 per entry in `survivingInstances` (baseColor,
    // then vec4(metallic, roughness, 0, 0)), in the same order given.
    // Deliberately takes the *already-filtered* survivor list rather than
    // re-deriving "which shapes survive" itself (that would be a second,
    // could-diverge copy of buildBlasFor()'s own real filter to keep in
    // sync by hand) -- rebuild() calls this against exactly the instances
    // that already got a real, non-null BlasEntry that same call, so
    // there is exactly one real filter in this class, not two. Public
    // and static specifically so this packing step is directly testable
    // without a live Vulkan device.
    [[nodiscard]] static std::vector<glm::vec4> packMaterials(const std::vector<Instance>& survivingInstances);

    // Real storage buffer -- binding 3 of sceneDescriptorSetLayout_ (see
    // Renderer::createSceneDescriptorResources()), read by
    // scene_rt.frag's traceReflection() and indexed by a ray-query hit's
    // own instanceCustomIndex (see rebuild()'s own comment on how that
    // index is assigned in lockstep with this buffer's contents).
    [[nodiscard]] VkBuffer materialsBuffer() const { return materialsBuffer_.buffer; }

    // Real, per-frame rebuild: (re)builds a real BLAS for any newly-seen
    // real (kind, params) shape (cached thereafter -- the expensive part
    // is NOT redone every frame), then always rebuilds the real TLAS
    // instance buffer and calls a real vkCmdBuildAccelerationStructuresKHR
    // for the TLAS itself (correct, standard practice for a dynamic
    // scene -- BLAS rebuild-every-frame would not be, TLAS rebuild-every-
    // frame is). A real, honest no-op if `instances` is empty (leaves the
    // previous frame's TLAS, if any, bound and valid rather than
    // destroying/recreating a zero-instance one that scene.frag would
    // then have nothing real to trace against).
    void rebuild(const std::vector<Instance>& instances);

    [[nodiscard]] VkAccelerationStructureKHR tlas() const { return tlas_; }
    [[nodiscard]] bool hasValidTlas() const { return tlas_ != VK_NULL_HANDLE; }

private:
    struct ShapeKey {
        MeshSourceKind kind;
        glm::vec3 params;
        bool operator==(const ShapeKey& other) const;
    };
    struct ShapeKeyHash {
        size_t operator()(const ShapeKey& key) const;
    };

    struct Buffer {
        VkBuffer buffer = VK_NULL_HANDLE;
        VmaAllocation allocation = nullptr;
        VkDeviceAddress address = 0;
    };

    struct BlasEntry {
        VkAccelerationStructureKHR blas = VK_NULL_HANDLE;
        Buffer asBuffer;
        Buffer vertexBuffer;
        Buffer indexBuffer;
        VkDeviceAddress blasAddress = 0;
    };

    [[nodiscard]] Buffer createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VmaMemoryUsage memUsage,
                                       VmaAllocationCreateFlags flags, bool wantAddress);
    void destroyBuffer(Buffer& buffer);
    [[nodiscard]] VkDeviceAddress bufferDeviceAddress(VkBuffer buffer) const;
    [[nodiscard]] const BlasEntry* getOrBuildBlas(const ShapeKey& key);
    [[nodiscard]] BlasEntry buildBlasFor(const ShapeKey& key);
    void destroyTlas();
    // Real, one-shot command buffer submit+wait -- the same
    // "small number of real build commands, synchronous, not pipelined"
    // simplification core::Mesh's own uploadToDeviceLocalBuffer() already
    // established for this codebase's procedural-geometry uploads.
    void submitAndWait(const std::function<void(VkCommandBuffer)>& record);

    VmaAllocator allocator_ = nullptr;
    VkDevice device_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    VkCommandPool cmdPool_ = VK_NULL_HANDLE;

    std::unordered_map<ShapeKey, BlasEntry, ShapeKeyHash> blasCache_;

    VkAccelerationStructureKHR tlas_ = VK_NULL_HANDLE;
    Buffer tlasBuffer_;
    Buffer tlasScratch_;
    Buffer tlasInstanceBuffer_;
    VkDeviceSize tlasBufferCapacity_ = 0;
    VkDeviceSize tlasScratchCapacity_ = 0;
    VkDeviceSize tlasInstanceCapacity_ = 0;
    bool initialized_ = false;

    // Kronos ("Rendering Fidelity Foundation" Phase 1.3) -- see
    // materialsBuffer()'s own comment. materials_ is this frame's real,
    // host-side packed data (also what packMaterials() itself returns);
    // materialsBuffer_ is its GPU-visible upload, resized (never shrunk,
    // same "avoid alloc/free churn" convention tlasInstanceBuffer_ above
    // already uses) whenever a frame needs more room than it currently has.
    std::vector<glm::vec4> materials_;
    Buffer materialsBuffer_;
    VkDeviceSize materialsBufferCapacity_ = 0;
};

} // namespace engine::core
