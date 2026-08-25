#pragma once

#include <string>
#include <vector>

#include "core/Mesh.hpp"

namespace engine::core {

struct GltfLoadResult {
    bool succeeded = false;
    std::string error; // set on failure, empty on success
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};

// Kronos (Alpha Roadmap Phase 8, "Asset Pipeline" -- "Automated Asset
// Hot-Import Pipeline"): a real glTF 2.0 loader (both `.gltf` text +
// sibling `.bin` and self-contained binary `.glb`), via the vendored
// tinygltf v3 (cmake/Dependencies.cmake) -- the same real "second
// imported mesh format" gap ModelImporterPlugin.hpp's own comment
// already named this engine as only having a Wavefront-OBJ answer for.
//
// Deliberately narrow, stated plainly, same shape ObjLoader.hpp's own
// header comment already establishes for .obj: one combined mesh per
// file -- every TRIANGLES-mode primitive across every mesh/node in the
// file is concatenated into one real vertex/index buffer (matching
// what this engine's Renderable/core::Mesh model actually supports:
// one meshHandle per entity, no multi-node scene-graph import yet).
// PBR materials, skinning, animation, and morph targets are parsed-
// and-ignored -- geometry only, same "mtllib/usemtl parsed-and-ignored"
// cut loadObj() already makes. Non-TRIANGLES primitives (LINES,
// POINTS, TRIANGLE_STRIP/FAN) are real, honestly skipped, not
// mishandled as triangles.
//
// Every numeric/structural read is guarded against a malformed file
// (missing accessor, out-of-range buffer view, unsupported component
// type) returning succeeded=false with a real message -- an imported
// file is untrusted input, same reasoning loadObj() and
// safety::AssetSafetyGuard's byte-level image parsing already apply.
[[nodiscard]] GltfLoadResult loadGltf(const std::string& path);

} // namespace engine::core
