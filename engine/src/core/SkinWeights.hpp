#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace engine::core {

// Standard real-time GPU skinning convention: up to 4 joint influences
// per vertex (jointIndices/weights, parallel arrays; jointIndices[k]==-1
// means "unused influence slot"). This is the same fixed-width-4 scheme
// every mainstream real-time engine uses (glTF's JOINTS_0/WEIGHTS_0
// included) -- more influences per vertex is a real, separate feature
// (glTF's JOINTS_1/WEIGHTS_1 for an 8-influence extension) this doesn't
// build.
struct VertexSkinWeights {
    std::array<int, 4> jointIndices{-1, -1, -1, -1};
    std::array<float, 4> weights{0.0f, 0.0f, 0.0f, 0.0f};
};

// Per-vertex skin weights for one mesh -- one VertexSkinWeights per
// vertex, same indexing as the mesh's own vertex array (perVertex[i]
// corresponds to that mesh's Vertex i). Pure data: no Vulkan/GPU-buffer
// ownership here, same "pure data vs GPU application" separation as
// core::Prefab/core::SceneFile -- see RiggedMesh.hpp for the half that
// uploads this to the GPU.
class SkinWeights {
public:
    std::vector<VertexSkinWeights> perVertex;

    // Real weight normalization -- rescales each vertex's 4 weights so
    // they sum to 1.0 (skips a vertex whose weights already sum to ~1,
    // and leaves an all-zero vertex alone rather than dividing by zero --
    // see validate()'s "zero-weight vertex" case for that one). A
    // skinning shader that reads un-normalized weights produces a
    // visibly shrunken/stretched mesh at that vertex, not a crash -- a
    // real, silent correctness bug this exists to prevent.
    void normalizeWeights();

    // Real, structural validation (missing joints / invalid weights /
    // zero-weight vertices):
    //  - perVertex.size() must equal `expectedVertexCount` (a mismatch
    //    means this SkinWeights wasn't authored for this mesh at all).
    //  - every non-unused jointIndices[k] (!= -1) must be in
    //    [0, jointCount) -- "missing joints": an index referencing a
    //    joint that doesn't exist in the skeleton this mesh is bound to.
    //  - every weights[k] must be finite and >= 0 (negative weights are
    //    nonsensical for a convex blend).
    //  - every vertex's weights must sum to a positive number -- an
    //    all-zero-weight vertex has no influence from any joint at all
    //    and would collapse to the origin under skinning.
    // Returns false and fills `outError` (including the offending vertex
    // index where relevant) on the first problem found.
    [[nodiscard]] bool validate(size_t expectedVertexCount, int jointCount, std::string& outError) const;
};

} // namespace engine::core
