#pragma once

#include <string>
#include <vector>

#include "core/Mesh.hpp"

namespace engine::core {

struct KMeshLoadResult {
    bool succeeded = false;
    std::string error; // set on failure, empty on success
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};

// Kronos ("3D Model Maker" Phase 4): the native mesh format -- a real,
// hand-rolled text format, the same "KEY value per line, END terminator"
// convention core::PluginManifest/core::SceneFile already use, not a new
// serialization technique introduced just for this. Unlike .obj (which
// only stores position/uv/normal, no material/topology metadata),
// this is the one real format that round-trips an EditableMesh-produced
// shape exactly, including any real topology an .obj export would also
// preserve -- the point of having both is real, honest: .obj is for
// getting a mesh in/out of other tools, .kmesh is Kronos's own.
//
// Format:
//   KMESH 1
//   VERTEXCOUNT <n>
//   V px py pz nx ny nz u v      (one line per vertex, n lines)
//   INDEXCOUNT <m>
//   F i0 i1 i2                    (one line per triangle, m/3 lines)
//   END
[[nodiscard]] bool saveKMesh(const std::string& path, const std::vector<Vertex>& vertices,
                              const std::vector<uint32_t>& indices);
[[nodiscard]] KMeshLoadResult loadKMesh(const std::string& path);

} // namespace engine::core
