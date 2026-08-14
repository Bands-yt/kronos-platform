#pragma once

#include <string>
#include <vector>

#include "core/Mesh.hpp"

namespace engine::core {

struct ObjLoadResult {
    bool succeeded = false;
    std::string error; // set on failure, empty on success
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};

// A real, minimal Wavefront OBJ (.obj) parser -- the first real *imported*
// (not procedurally generated, not authored via Studio's own primitive
// generators) mesh format this engine can load, per docs/ARCHITECTURE.md
// §7's still-stubbed migration::AssetConverter mesh path. Handles
// positions (v), texture coordinates (vt), normals (vn), and faces (f),
// including OBJ's negative/relative index convention and n-gon faces
// (triangulated by fan triangulation from each face's first vertex).
//
// Deliberately narrow, stated plainly: one mesh per file -- o/g/s
// grouping and mtllib/usemtl material assignment are parsed-and-ignored,
// not respected (this produces one core::Mesh, not a multi-material
// scene graph). Files with no vn data get real flat face normals
// computed here (the same per-triangle-accumulate technique
// Mesh::createBox's flat-shaded faces rely on), not a placeholder
// up-vector. Runs the result through computeTangents() the same as every
// procedural generator (see Mesh::uploadFromHost()) once uploaded, so
// imported meshes get real tangent-space normal mapping too.
//
// Every numeric parse is guarded (malformed/truncated files return
// succeeded=false with a message, never an uncaught exception or a
// crash) -- an imported file is untrusted input, the same reasoning
// safety::AssetSafetyGuard's byte-level parsing already applies to
// uploaded images.
[[nodiscard]] ObjLoadResult loadObj(const std::string& path);

// The real inverse of loadObj() -- writes `vertices`/`indices` (any
// source: EditableMesh::vertices()/indices(), a loadObj() result passed
// straight through, anything shaped like Mesh::uploadFromHost()'s own
// parameters) as a real, standard Wavefront OBJ: one `v`/`vt`/`vn` line
// per vertex plus one 1-indexed `f v/vt/vn ...` line per triangle --
// loadObj() itself can read the result straight back. Returns false on
// a real file-open failure (bad path, no write permission); never
// throws.
[[nodiscard]] bool saveObj(const std::string& path, const std::vector<Vertex>& vertices,
                            const std::vector<uint32_t>& indices);

} // namespace engine::core
