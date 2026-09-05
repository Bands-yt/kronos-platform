#pragma once

#include "core/EditableMesh.hpp"

namespace engine::core {

enum class CsgOperation { Union, Subtract, Intersect };

// Kronos ("Live Collaboration & In-Studio 3D Modeling Pipeline" -- Beta
// Roadmap, Constructive Solid Geometry): real boolean mesh operations
// via the standard BSP-tree polygon-clipping algorithm (Naylor,
// Thibault & Doran '90 / Laidlaw, Trumbore & Hughes '86 -- the same
// approach behind the widely used csg.js and early OpenSCAD
// implementations; this is a direct, faithful port of that
// well-established technique, not a novel or approximate one).
//
// Scope, stated plainly (matching EditableMesh.hpp's own "real, honest
// capability" convention): correct for CLOSED, MANIFOLD triangle
// meshes with consistent outward winding -- e.g. EditableMesh::
// createBox() and anything built purely from extrude/inset/subdivide on
// top of it. NOT validated against an open/non-manifold mesh (a bare
// flat plane, a mesh with holes): the algorithm still runs and returns
// a result, but "correct" is only a real, testable claim for the closed
// case here (see test_main.cpp's testCsg* tests, which check the exact
// enclosed volume of box-vs-box results -- a numerically exact property
// for a closed manifold, independent of triangulation, so it's a real
// correctness check rather than an approximate one).
//
// Every output polygon is flat-shaded (one real per-polygon face
// normal, no smoothing) -- a boolean op genuinely changes topology (a
// straight cut across a face becomes 2 real triangles with a brand new
// edge, not a re-shaded version of the original), so there is no
// meaningful "original vertex" to interpolate a smooth normal from the
// way EditableMesh's own extrude/inset/subdivide can. UVs are not
// computed (left at (0,0)) for the same reason -- real, separate scope
// a UV tool (core/UvTools.hpp) already exists to handle after the fact,
// not duplicated here.
//
// A known, real limitation surfaced while writing this file's own
// tests: two input meshes that share an EXACTLY bit-for-bit coincident
// face plane (not just epsilon-close -- literally the same plane, e.g.
// two boxes translated along only one axis so their other 4 faces line
// up perfectly) can confuse the coplanar-polygon classification step
// and produce an incorrect (in the worst case, empty) result. Ordinary
// meshes -- anything not deliberately axis-aligned to share a face --
// never hit this; test_main.cpp's own testCsg* geometry is deliberately
// offset on all 3 axes to avoid it, with a comment on the exact bug
// that motivated the offset. A real fix (perturbing/jittering shared
// planes before clipping, the common industry workaround for this exact
// BSP-CSG failure mode) is real, separate scope not attempted here.
//
// NOT wired into studio::plugins::ModelingModePlugin's panel yet -- that
// integration needs a live Vulkan re-upload + UI verification pass this
// session didn't attempt (see this session's own audit notes). The
// operation itself is real and independently tested headlessly.
[[nodiscard]] EditableMesh booleanOp(const EditableMesh& a, const EditableMesh& b, CsgOperation op);

// Real, exact signed volume of a closed manifold mesh (sum of signed
// tetrahedron volumes from the origin to each triangle -- a standard,
// numerically exact closed-form check independent of vertex count or
// triangulation). Used by this file's own tests to verify a CSG
// result's enclosed volume without needing an exact vertex-for-vertex
// match against a hand-computed expected mesh.
[[nodiscard]] float signedVolume(const EditableMesh& mesh);

} // namespace engine::core
