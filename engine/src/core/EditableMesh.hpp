#pragma once

#include <array>
#include <cstdint>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include "core/Mesh.hpp"

namespace engine::core {

// Kronos ("3D Model Maker" Phase 2 -- real vertex/edge/face editing):
// the CPU-retained, editable counterpart to core::Mesh. Mesh itself
// keeps no host-side vertex/index data after uploadFromHost() (see that
// class's own comment -- only GPU buffer handles + bounds survive) --
// real topology editing (extrude/subdivide/merge/bevel/inset) needs the
// actual vertex positions and triangle connectivity to still exist on
// the CPU, so this is a deliberately separate class, not an extension of
// Mesh. Convert to a real, renderable core::Mesh via Mesh::uploadFromHost()
// (called with vertices()/indices() directly -- they're already the
// exact types that function takes) whenever the edited result needs to
// go back on screen; this class itself has no Vulkan dependency at all,
// so every operation below is pure and headlessly testable (see
// engine/tests/test_main.cpp), the same "pure logic, separate GPU-owning
// caller" split studio/CreatorToolsSpawning.cpp already establishes.
//
// Scope, stated plainly (matching this codebase's "real, honest
// capability" convention rather than a half-general implementation):
// every operation below is real and correct for a well-formed,
// manifold-ish mesh (each edge shared by at most 2 triangles) -- the
// realistic case for anything Block Builder or an .obj import produces.
// bevelEdge() specifically only handles the standard interior-edge case
// (shared by exactly 2 faces); a boundary edge (1 face) or a non-manifold
// edge (3+ faces) is a real, honest no-op (returns false), not a guess.
class EditableMesh {
public:
    enum class SelectionMode { Vertex, Edge, Face };

    [[nodiscard]] static EditableMesh fromVertexData(std::vector<Vertex> vertices, std::vector<uint32_t> indices);
    // A real, editable unit box (half-extents 0.5) -- the same 24-vertex
    // flat-shaded-per-face layout Mesh::createBox() uploads, just kept on
    // the CPU here instead of going straight to the GPU. Used as Modeling
    // Mode's own "start from a box" entry point. `center` defaults to the
    // origin (every pre-existing call site is unaffected); a non-zero
    // center is what ModelingModePlugin's own CSG panel uses to place a
    // real second box operand anywhere relative to the mesh being edited,
    // without needing a second seed constructor.
    [[nodiscard]] static EditableMesh createBox(glm::vec3 halfExtents, glm::vec3 center = glm::vec3(0.0f));

    [[nodiscard]] const std::vector<Vertex>& vertices() const { return vertices_; }
    [[nodiscard]] const std::vector<uint32_t>& indices() const { return indices_; }
    // The one real, intentional mutation point outside the topology ops
    // below -- lets a UV tool (core/UvTools.hpp) rewrite a vertex's real
    // UV coordinate without exposing the whole vertex list as mutable.
    void setVertexUv(uint32_t index, glm::vec2 uv) {
        if (index < vertices_.size()) vertices_[index].uv = uv;
    }
    // The same real, narrow mutation point as setVertexUv() above, for
    // position instead -- what a script-driven "runtime vertex
    // deformation" caller (core::ScriptMeshApi) needs that no existing
    // topology op already provides (every op above moves vertices as a
    // side effect of a specific topology change, not an arbitrary
    // single-vertex move).
    void setVertexPosition(uint32_t index, glm::vec3 position) {
        if (index < vertices_.size()) vertices_[index].position = position;
    }
    [[nodiscard]] glm::vec3 boundsMin() const;
    [[nodiscard]] glm::vec3 boundsMax() const;
    [[nodiscard]] size_t vertexCount() const { return vertices_.size(); }
    [[nodiscard]] size_t faceCount() const { return indices_.size() / 3; }

    // A face's 3 real vertex indices, in winding order. Returns
    // {0,0,0} for an out-of-range faceIndex -- callers that already
    // guard with faceIndex < faceCount() never hit this.
    [[nodiscard]] std::array<uint32_t, 3> faceVertexIndices(size_t faceIndex) const;
    [[nodiscard]] glm::vec3 faceCentroid(size_t faceIndex) const;
    [[nodiscard]] glm::vec3 faceNormal(size_t faceIndex) const;

    // Every real, deduplicated edge in the mesh, as (lower index, higher
    // index) pairs -- the same "an edge is identified by its two
    // endpoint vertex indices" convention bevelEdge() itself uses, so a
    // caller (Modeling Mode's own edge list) can enumerate real edges to
    // select from without building a second representation.
    [[nodiscard]] std::vector<std::pair<uint32_t, uint32_t>> allEdges() const;

    // Real, standard extrude-along-normal: duplicates `faceIndex`'s 3
    // vertices offset by `distance` along the face's own normal, moves
    // the face to the offset cap, and fills the 3 real quad walls
    // connecting the original edge loop to the new one (6 new
    // triangles). Face count: +6 (1 face becomes 7: the moved cap + 3
    // wall quads x 2 triangles). Returns false for an out-of-range
    // faceIndex, true otherwise.
    bool extrudeFace(size_t faceIndex, float distance);

    // Real 1-to-4 triangle subdivision: inserts a vertex at each of the
    // face's 3 edge midpoints and replaces the 1 triangle with 4 (three
    // corner triangles + one center triangle). Face count: +3. Returns
    // false for an out-of-range faceIndex.
    bool subdivideFace(size_t faceIndex);

    // Real vertex welding: every pair of vertices within
    // `distanceThreshold` of each other is merged into one (the
    // lower-indexed survivor), every face index remapped, any
    // now-degenerate face (two or more indices equal after remapping)
    // removed, and any vertex no longer referenced by a real face
    // dropped. Returns the real number of vertices removed (0 if
    // nothing was close enough to merge).
    size_t mergeVertices(float distanceThreshold);

    // Real interior-edge bevel: replaces the shared edge between
    // exactly 2 adjacent triangles with a new flat quad strip, offset
    // `amount` (0..1, a fraction of the distance to each adjacent
    // face's own opposite vertex) toward each side. See class comment
    // for why a boundary/non-manifold edge is a real, honest no-op
    // instead of a guess. `v0`/`v1` identify the edge by its two real
    // vertex indices (order doesn't matter).
    bool bevelEdge(uint32_t v0, uint32_t v1, float amount);

    // Real inset: shrinks `faceIndex`'s own 3 vertices toward its
    // centroid by `amount` (0..1), replacing the face with the smaller
    // inset triangle and filling the 3 real quad walls connecting the
    // original edge loop to the inset one (same wall-building shape as
    // extrudeFace(), with no normal-direction offset). Face count: +6.
    // Returns false for an out-of-range faceIndex.
    bool insetFace(size_t faceIndex, float amount);

private:
    std::vector<Vertex> vertices_;
    std::vector<uint32_t> indices_;
};

} // namespace engine::core
