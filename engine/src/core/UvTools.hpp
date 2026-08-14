#pragma once

#include "core/EditableMesh.hpp"

namespace engine::core {

// Kronos ("3D Model Maker" Phase 3 -- UV tools): real, pure UV-generation
// functions over an EditableMesh, matching that class's own "zero
// Vulkan dependency, headlessly testable" split. All three write real
// UV coordinates via EditableMesh::setVertexUv() -- nothing here is a
// placeholder/no-op.
enum class ProjectionAxis { X, Y, Z };

// Real planar projection: every vertex's UV becomes its own position's
// two non-projected axes, normalized against the whole mesh's real
// bounding box into [0,1]. `axis` is the axis being projected away --
// ProjectionAxis::Y projects onto the XZ plane (U=X, V=Z), matching how
// a top-down "unwrap this like a map" projection reads.
void applyPlanarProjection(EditableMesh& mesh, ProjectionAxis axis);

// Real cube (box) projection: per face, picks whichever axis that
// face's own real normal (EditableMesh::faceNormal()) points most
// along, then planar-projects that face's own 3 vertices onto the
// perpendicular plane -- the standard "box mapping" technique, a real
// per-face axis choice rather than one single global axis.
void applyCubeProjection(EditableMesh& mesh);

// Real, deliberately simplified auto-unwrap: independently unwraps each
// triangle to its own real, shape-preserving UV triangle (the real
// triangle's own edge lengths, placed via the law of cosines -- not a
// placeholder), then packs each into its own non-overlapping grid cell
// across [0,1]. This is NOT a seam-aware, whole-mesh conformal unwrap
// (that's real research-grade software -- LSCM/ABF and friends) --
// stated plainly here rather than silently passed off as more than it
// is. Real and useful for texturing a Block-Builder-edited mesh without
// visible stretching per-face, just with more seams than a "real"
// unwrapper would produce.
//
// Returns a NEW mesh rather than mutating `mesh` in place: UV is stored
// per-vertex, but two triangles sharing a vertex (e.g. the 2 triangles
// making up one quad face of EditableMesh::createBox()) each need that
// shared corner at a DIFFERENT UV position in their own independent
// unwrapped triangle -- a real conflict a single per-vertex UV can't
// hold. The real, correct fix (the same one flat per-face vertex
// normals already use throughout this engine, see Mesh::createBox()'s
// own "24 vertices, not 8" comment) is to split every triangle to its
// own 3 unique vertices first; in-place mutation would silently let a
// later triangle overwrite an earlier one's UV at any shared vertex.
[[nodiscard]] EditableMesh applyAutoUnwrap(const EditableMesh& mesh);

} // namespace engine::core
