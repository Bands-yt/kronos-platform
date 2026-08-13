#pragma once

#include <vector>

#include <glm/glm.hpp>
#include <volk.h>
#include <vk_mem_alloc.h>

#include "core/Mesh.hpp"
#include "core/SkinWeights.hpp"
#include "core/Skeleton.hpp"

namespace engine::core {

// Real per-vertex skin data uploaded as a second vertex buffer bound
// alongside the base Vertex buffer -- same "binding 0 = base Vertex,
// binding 1 = per-draw extra data" shape SceneTypes.hpp's InstanceData/
// ParticleInstanceData already use for their own pipelines (see that
// file), applied here to skin weights instead of per-instance
// transforms. ivec4 joint indices + vec4 weights, a direct GPU-friendly
// reinterpretation of core::VertexSkinWeights, not a different shape.
struct GpuSkinVertex {
    glm::ivec4 jointIndices{-1, -1, -1, -1};
    glm::vec4 weights{0.0f};

    static VkVertexInputBindingDescription bindingDescription();
    // `startLocation` matches every other "extra per-vertex/per-instance
    // data" attribute block in this codebase starting at location 4 (the
    // base Vertex struct owns locations 0-3) -- see scene_instanced.vert/
    // particle.vert's own attribute numbering.
    static std::vector<VkVertexInputAttributeDescription> attributeDescriptions(uint32_t startLocation);
};

// A mesh + the skeleton it's bound to + its per-vertex skin weights --
// the real content description a GPU-skinned draw needs. Combines
// core::Mesh (the same GPU-resident vertex/index buffers every other
// mesh in this engine uses) with a second, skin-data vertex buffer and
// the core::Skeleton it was authored against.
//
// Deliberately does NOT own the skeleton's *current pose* (the
// joint-local transforms during playback) -- that's
// core::AnimationPlayer's job (see AnimationPlayer.hpp); RiggedMesh is
// the static bind-time data (mesh + bind skeleton + weights), reused by
// every instance of this rigged asset the same way core::MeshLibrary's
// handles are reused today.
class RiggedMesh {
public:
    // Real validation before any GPU work: SkinWeights::validate()
    // against `vertices.size()`/`skeleton.joints.size()`, and
    // skeleton.validate() -- a bad rig fails here with a specific error,
    // not partway through a GPU upload.
    [[nodiscard]] bool uploadFromHost(VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue,
                                       const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices,
                                       const SkinWeights& skinWeights, Skeleton skeleton, std::string& outError);
    void destroy(VmaAllocator allocator);

    [[nodiscard]] const Mesh& mesh() const { return mesh_; }
    [[nodiscard]] const Skeleton& skeleton() const { return skeleton_; }
    [[nodiscard]] VkBuffer skinBuffer() const { return skinBuffer_; }

private:
    Mesh mesh_;
    Skeleton skeleton_;

    VkBuffer skinBuffer_ = VK_NULL_HANDLE;
    VmaAllocation skinAllocation_ = nullptr;
};

// Real CPU skinning -- computes skinned vertex positions/normals for
// `vertices`/`skinWeights` at a specific pose (`skinningMatrices`, one
// matrix per joint: skinningMatrices[j] = currentJointWorldMatrix[j] *
// skeleton.inverseBindMatrices()[j], already composed -- see
// AnimationPlayer::computeSkinningMatrices()). Used for two real,
// distinct purposes: the Upload pipeline's "pose snapshot" thumbnail (a
// single static frame doesn't need the GPU skinning pipeline stood up
// at all) and a real "compare CPU vs GPU" correctness test -- a GPU
// skinning bug (wrong matrix order, wrong weight normalization) shows up
// as a mismatch between this function's output and the GPU pipeline's,
// not as a crash, so having a real independent CPU implementation is
// what makes that bug class testable at all.
//
// Returns a real, regular vertex list -- skinning has already happened,
// so the result uploads through core::Mesh::uploadFromHost() like any
// static mesh, no special-casing needed downstream.
[[nodiscard]] std::vector<Vertex> skinVerticesCPU(const std::vector<Vertex>& vertices, const SkinWeights& skinWeights,
                                                    const std::vector<glm::mat4>& skinningMatrices);

// Handle-based registry for RiggedMesh, same shape (and same "no
// removal, index-based handles" reasoning) as core::MeshLibrary -- see
// its header comment.
class RiggedMeshLibrary {
public:
    static constexpr uint32_t kInvalidHandle = ~0u;

    uint32_t registerRiggedMesh(RiggedMesh riggedMesh);
    [[nodiscard]] const RiggedMesh* get(uint32_t handle) const;
    void destroyAll(VmaAllocator allocator);

private:
    std::vector<RiggedMesh> riggedMeshes_;
};

} // namespace engine::core
