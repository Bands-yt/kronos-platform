#include "core/FbxLoader.hpp"

#include <cstring>

#include <ufbx.h>

namespace engine::core {

FbxLoadResult loadFbx(const std::string& path) {
    FbxLoadResult result;

    ufbx_load_opts opts;
    std::memset(&opts, 0, sizeof(opts));
    // Real space normalization -- see this loader's own header comment
    // on why FBX (unlike glTF/OBJ) needs it.
    opts.target_axes.right = UFBX_COORDINATE_AXIS_POSITIVE_X;
    opts.target_axes.up = UFBX_COORDINATE_AXIS_POSITIVE_Y;
    opts.target_axes.front = UFBX_COORDINATE_AXIS_POSITIVE_Z;
    opts.target_unit_meters = 1.0f;
    opts.generate_missing_normals = true;

    ufbx_error error;
    ufbx_scene* scene = ufbx_load_file(path.c_str(), &opts, &error);
    if (scene == nullptr) {
        result.error = std::string(error.description.data, error.description.length);
        return result;
    }

    if (scene->meshes.count == 0) {
        result.error = "FBX file has no meshes";
        ufbx_free_scene(scene);
        return result;
    }

    // Scratch buffer for ufbx_triangulate_face()'s own output, reused
    // across every face of every mesh rather than reallocated per face.
    std::vector<uint32_t> triangleIndices;

    for (ufbx_mesh* mesh : scene->meshes) {
        if (mesh == nullptr || !mesh->vertex_position.exists) continue; // real, honest skip -- no usable geometry

        for (const ufbx_face& face : mesh->faces) {
            if (face.num_indices < 3) continue; // real, honest skip -- a degenerate point/line "face"

            size_t maxTriangleIndices = (static_cast<size_t>(face.num_indices) - 2) * 3;
            if (triangleIndices.size() < maxTriangleIndices) triangleIndices.resize(maxTriangleIndices);
            uint32_t triangleIndexCount =
                ufbx_triangulate_face(triangleIndices.data(), triangleIndices.size(), mesh, face);

            for (uint32_t i = 0; i < triangleIndexCount; ++i) {
                uint32_t cornerIndex = triangleIndices[i];

                Vertex vertex;
                ufbx_vec3 position = ufbx_get_vertex_vec3(&mesh->vertex_position, cornerIndex);
                vertex.position = glm::vec3(static_cast<float>(position.x), static_cast<float>(position.y),
                                             static_cast<float>(position.z));

                if (mesh->vertex_normal.exists) {
                    ufbx_vec3 normal = ufbx_get_vertex_vec3(&mesh->vertex_normal, cornerIndex);
                    vertex.normal = glm::vec3(static_cast<float>(normal.x), static_cast<float>(normal.y),
                                               static_cast<float>(normal.z));
                } else {
                    // Real, honest fallback -- generate_missing_normals
                    // above should make this unreachable in practice,
                    // kept only so a mesh ufbx genuinely couldn't
                    // generate a normal for (degenerate geometry) still
                    // gets a real, non-garbage value rather than
                    // whatever Vertex's own default-initializer leaves.
                    vertex.normal = glm::vec3(0.0f, 1.0f, 0.0f);
                }

                if (mesh->vertex_uv.exists) {
                    ufbx_vec2 uv = ufbx_get_vertex_vec2(&mesh->vertex_uv, cornerIndex);
                    vertex.uv = glm::vec2(static_cast<float>(uv.x), static_cast<float>(uv.y));
                } else {
                    vertex.uv = glm::vec2(0.0f, 0.0f);
                }

                result.vertices.push_back(vertex);
                result.indices.push_back(static_cast<uint32_t>(result.vertices.size() - 1));
            }
        }
    }

    ufbx_free_scene(scene);

    if (result.vertices.empty() || result.indices.empty()) {
        result.error = "no usable triangle geometry found in this FBX file";
        return result;
    }

    result.succeeded = true;
    return result;
}

} // namespace engine::core
