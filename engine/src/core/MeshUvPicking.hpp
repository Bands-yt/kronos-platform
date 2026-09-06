#pragma once

#include <cstddef>

#include <glm/glm.hpp>

namespace engine::core {

class EditableMesh;

// Kronos ("Vulkan Compute PBR Painter" -- v0.4.0 Creator Suite): the
// real gap core::ScenePicking.hpp's own pickEntity() leaves open for
// on-mesh painting -- that function tests ray-vs-AABB only, and its
// `point` is where the ray crosses the box, not a real point on the
// mesh's actual surface (see ScenePicking.hpp's own header comment). A
// compute painter needs the exact UV under the cursor to know which
// texel to stamp; only a real per-triangle test with barycentric
// interpolation can give it that.
struct MeshUvPickResult {
    bool hit = false;
    size_t faceIndex = 0;
    float distance = 0.0f; // real distance along `direction` (not normalized to [0,1] the way ScenePickResult's AABB t is)
    glm::vec3 point{0.0f};  // real surface hit point, in the same space as the mesh's own vertex positions
    glm::vec2 uv{0.0f};     // real, barycentric-interpolated UV at the hit point -- where a compute painter stamps
    glm::vec3 normal{0.0f}; // barycentric-interpolated (smooth) normal at the hit point
};

// Real Moller-Trumbore ray-triangle intersection against every real
// triangle in `mesh` (EditableMesh's own CPU-retained vertex/index data
// -- see that class's own header comment for why core::Mesh itself has
// no host-side data left to test against once uploaded to the GPU),
// returning the CLOSEST real hit along the ray within [0, maxDistance],
// or `.hit == false` if none. Zero Vulkan dependency, same
// "headlessly testable" split EditableMesh's own topology operations
// already establish. `origin`/`direction` are in the SAME space as
// `mesh`'s own vertex positions -- the caller transforms a world-space
// ray into an entity's local space first, the exact division of
// responsibility core::pickEntity() already uses for its own AABB test.
[[nodiscard]] MeshUvPickResult pickTriangleUv(const EditableMesh& mesh, glm::vec3 origin, glm::vec3 direction,
                                               float maxDistance);

} // namespace engine::core
