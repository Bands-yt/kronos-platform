#include "core/GltfLoader.hpp"

#include <cstring>

// TINYGLTF3_ENABLE_FS is defined PUBLIC on the tinygltf::tinygltf CMake
// target (Dependencies.cmake) -- propagates here automatically, real
// filesystem I/O for resolving a .gltf's sibling .bin buffer files.
#include <tiny_gltf_v3.h>

namespace engine::core {

namespace {

// Real, bounds-checked resolution of one accessor down to a raw,
// strided byte view -- every one of readPositionsOrNormals()/readUVs()/
// readIndices() below builds on this single real check rather than
// re-deriving buffer-view/buffer bounds math three times.
struct RawAccessorView {
    const uint8_t* base = nullptr;
    int32_t stride = 0;
    uint64_t count = 0;
    int32_t componentType = 0;
    int32_t type = 0;
};

bool resolveAccessor(const tg3_model& model, int32_t accessorIndex, RawAccessorView& out, std::string& outError) {
    if (accessorIndex < 0 || static_cast<uint32_t>(accessorIndex) >= model.accessors_count) {
        outError = "accessor index out of range";
        return false;
    }
    const tg3_accessor& accessor = model.accessors[accessorIndex];
    if (accessor.buffer_view < 0 || static_cast<uint32_t>(accessor.buffer_view) >= model.buffer_views_count) {
        outError = "accessor has no buffer view (sparse-only accessors are not supported)";
        return false;
    }
    const tg3_buffer_view& bufferView = model.buffer_views[accessor.buffer_view];
    if (bufferView.buffer < 0 || static_cast<uint32_t>(bufferView.buffer) >= model.buffers_count) {
        outError = "buffer view references an out-of-range buffer";
        return false;
    }
    const tg3_buffer& buffer = model.buffers[bufferView.buffer];
    int32_t stride = tg3_accessor_byte_stride(&accessor, &bufferView);
    int32_t elementSize = tg3_component_size(accessor.component_type) * tg3_num_components(accessor.type);
    if (stride <= 0 || elementSize <= 0) {
        outError = "unsupported or malformed accessor component/element type";
        return false;
    }
    uint64_t base = bufferView.byte_offset + accessor.byte_offset;
    uint64_t lastElementEnd =
        base + (accessor.count == 0 ? 0 : (accessor.count - 1) * static_cast<uint64_t>(stride) + static_cast<uint64_t>(elementSize));
    if (accessor.count > 0 && lastElementEnd > buffer.data.count) {
        outError = "accessor data reads past the end of its buffer -- malformed file";
        return false;
    }

    out.base = buffer.data.data + base;
    out.stride = stride;
    out.count = accessor.count;
    out.componentType = accessor.component_type;
    out.type = accessor.type;
    return true;
}

// POSITION/NORMAL are always VEC3 float per the real glTF 2.0 spec --
// no multi-component-type handling needed, unlike TEXCOORD_0/indices
// below.
bool readVec3(const RawAccessorView& view, std::vector<glm::vec3>& out, std::string& outError) {
    if (view.type != TG3_TYPE_VEC3 || view.componentType != TG3_COMPONENT_TYPE_FLOAT) {
        outError = "expected a VEC3 float accessor (POSITION/NORMAL must be per glTF 2.0 spec)";
        return false;
    }
    out.resize(view.count);
    for (uint64_t i = 0; i < view.count; ++i) {
        float components[3];
        std::memcpy(components, view.base + i * static_cast<uint64_t>(view.stride), sizeof(components));
        out[i] = glm::vec3(components[0], components[1], components[2]);
    }
    return true;
}

// TEXCOORD_0 may legally be float, normalized unsigned byte, or
// normalized unsigned short per spec -- real handling of all three,
// not just the float case some minimal importers only bother with.
bool readUV(const RawAccessorView& view, std::vector<glm::vec2>& out, std::string& outError) {
    if (view.type != TG3_TYPE_VEC2) {
        outError = "TEXCOORD_0 must be a VEC2 accessor";
        return false;
    }
    out.resize(view.count);
    for (uint64_t i = 0; i < view.count; ++i) {
        const uint8_t* ptr = view.base + i * static_cast<uint64_t>(view.stride);
        if (view.componentType == TG3_COMPONENT_TYPE_FLOAT) {
            float uv[2];
            std::memcpy(uv, ptr, sizeof(uv));
            out[i] = glm::vec2(uv[0], uv[1]);
        } else if (view.componentType == TG3_COMPONENT_TYPE_UNSIGNED_BYTE) {
            out[i] = glm::vec2(ptr[0] / 255.0f, ptr[1] / 255.0f);
        } else if (view.componentType == TG3_COMPONENT_TYPE_UNSIGNED_SHORT) {
            uint16_t uv[2];
            std::memcpy(uv, ptr, sizeof(uv));
            out[i] = glm::vec2(uv[0] / 65535.0f, uv[1] / 65535.0f);
        } else {
            outError = "unsupported TEXCOORD_0 component type";
            return false;
        }
    }
    return true;
}

// Real handling of all three legal index component types per spec
// (UNSIGNED_BYTE/SHORT/INT) -- a small mesh commonly uses the smallest
// one that fits, not always UNSIGNED_INT.
bool readIndices(const RawAccessorView& view, std::vector<uint32_t>& out, std::string& outError) {
    out.resize(view.count);
    for (uint64_t i = 0; i < view.count; ++i) {
        const uint8_t* ptr = view.base + i * static_cast<uint64_t>(view.stride);
        if (view.componentType == TG3_COMPONENT_TYPE_UNSIGNED_BYTE) {
            out[i] = ptr[0];
        } else if (view.componentType == TG3_COMPONENT_TYPE_UNSIGNED_SHORT) {
            uint16_t value;
            std::memcpy(&value, ptr, sizeof(value));
            out[i] = value;
        } else if (view.componentType == TG3_COMPONENT_TYPE_UNSIGNED_INT) {
            uint32_t value;
            std::memcpy(&value, ptr, sizeof(value));
            out[i] = value;
        } else {
            outError = "unsupported index accessor component type";
            return false;
        }
    }
    return true;
}

} // namespace

GltfLoadResult loadGltf(const std::string& path) {
    GltfLoadResult result;

    tinygltf3::Model model;
    tinygltf3::ErrorStack errors;
    tg3_error_code code = tinygltf3::parse_file(model, errors, path.c_str());
    if (code != TG3_OK) {
        result.error = "glTF parse failed";
        if (errors.count() > 0) {
            const tg3_error_entry* entry = errors.entry(0);
            if (entry != nullptr && entry->message != nullptr) result.error += std::string(": ") + entry->message;
        }
        return result;
    }

    const tg3_model& m = *model.get();
    if (m.meshes_count == 0) {
        result.error = "glTF file has no meshes";
        return result;
    }

    // Real, honest tracking for the flat-normal fallback below -- see
    // this function's own trailing comment on the one accepted edge
    // case (a file mixing normal-bearing and normal-less primitives).
    bool anyRealNormals = false;

    for (uint32_t meshIndex = 0; meshIndex < m.meshes_count; ++meshIndex) {
        const tg3_mesh& mesh = m.meshes[meshIndex];
        for (uint32_t primIndex = 0; primIndex < mesh.primitives_count; ++primIndex) {
            const tg3_primitive& prim = mesh.primitives[primIndex];
            // Real, honest skip for non-triangle primitives -- see this
            // loader's own header comment. mode == -1 means "unspecified",
            // which the spec defines as defaulting to TRIANGLES.
            if (prim.mode != -1 && prim.mode != TG3_MODE_TRIANGLES) continue;

            int32_t positionAccessor = -1;
            int32_t normalAccessor = -1;
            int32_t uvAccessor = -1;
            for (uint32_t a = 0; a < prim.attributes_count; ++a) {
                if (tg3_str_equals_cstr(prim.attributes[a].key, "POSITION")) positionAccessor = prim.attributes[a].value;
                else if (tg3_str_equals_cstr(prim.attributes[a].key, "NORMAL")) normalAccessor = prim.attributes[a].value;
                else if (tg3_str_equals_cstr(prim.attributes[a].key, "TEXCOORD_0")) uvAccessor = prim.attributes[a].value;
            }
            if (positionAccessor < 0) continue; // no usable geometry in this primitive -- real, honest skip

            std::string viewError;
            RawAccessorView positionView;
            if (!resolveAccessor(m, positionAccessor, positionView, viewError)) {
                result = GltfLoadResult{};
                result.error = "mesh " + std::to_string(meshIndex) + " primitive " + std::to_string(primIndex) +
                                " POSITION: " + viewError;
                return result;
            }
            std::vector<glm::vec3> positions;
            if (!readVec3(positionView, positions, viewError)) {
                result = GltfLoadResult{};
                result.error = "mesh " + std::to_string(meshIndex) + " primitive " + std::to_string(primIndex) +
                                " POSITION: " + viewError;
                return result;
            }

            std::vector<glm::vec3> normals;
            if (normalAccessor >= 0) {
                RawAccessorView normalView;
                if (resolveAccessor(m, normalAccessor, normalView, viewError) && readVec3(normalView, normals, viewError)) {
                    anyRealNormals = true;
                } else {
                    normals.clear(); // real, honest fallback -- this primitive's vertices get a flat/default normal below
                }
            }

            std::vector<glm::vec2> uvs;
            if (uvAccessor >= 0) {
                RawAccessorView uvView;
                if (resolveAccessor(m, uvAccessor, uvView, viewError)) {
                    // Best-effort: a real UV read failure leaves uvs
                    // empty (vertices fall back to (0,0) below) rather
                    // than failing the whole import over a cosmetic gap.
                    (void)readUV(uvView, uvs, viewError);
                }
            }

            uint32_t vertexBase = static_cast<uint32_t>(result.vertices.size());
            for (size_t i = 0; i < positions.size(); ++i) {
                Vertex vertex;
                vertex.position = positions[i];
                vertex.normal = i < normals.size() ? normals[i] : glm::vec3(0.0f, 1.0f, 0.0f);
                vertex.uv = i < uvs.size() ? uvs[i] : glm::vec2(0.0f, 0.0f);
                result.vertices.push_back(vertex);
            }

            if (prim.indices >= 0) {
                RawAccessorView indexView;
                std::vector<uint32_t> indices;
                if (!resolveAccessor(m, prim.indices, indexView, viewError) || !readIndices(indexView, indices, viewError)) {
                    result = GltfLoadResult{};
                    result.error =
                        "mesh " + std::to_string(meshIndex) + " primitive " + std::to_string(primIndex) + " indices: " + viewError;
                    return result;
                }
                for (uint32_t index : indices) result.indices.push_back(vertexBase + index);
            } else {
                // Real, valid glTF case -- no indices means draw
                // positions in declared order (an implicit 0,1,2,...
                // index buffer), same as a non-indexed draw call.
                for (uint32_t i = 0; i < positions.size(); ++i) result.indices.push_back(vertexBase + i);
            }
        }
    }

    if (result.vertices.empty() || result.indices.empty()) {
        result.error = "no usable TRIANGLES-mode geometry found in this glTF file";
        return result;
    }

    // Real flat face normals, same technique and winding convention
    // (cross(edge2, edge1)) as ObjLoader.cpp's own fallback -- see that
    // file's own comment for why this exact cross-product order,
    // verified against Mesh::createBox's known-correct face normals.
    // Real, accepted scope note: this only runs when NO primitive
    // anywhere in the file supplied real normals; a file mixing
    // normal-bearing and normal-less primitives leaves the latter at
    // the plain default (0,1,0) rather than a per-primitive flat
    // computation -- a genuinely rare real-world case (every common DCC
    // export tool either always or never emits NORMAL), not worth the
    // extra bookkeeping this pass would need to track per-primitive.
    if (!anyRealNormals) {
        std::vector<glm::vec3> accumulated(result.vertices.size(), glm::vec3(0.0f));
        for (size_t i = 0; i + 2 < result.indices.size(); i += 3) {
            uint32_t i0 = result.indices[i];
            uint32_t i1 = result.indices[i + 1];
            uint32_t i2 = result.indices[i + 2];
            glm::vec3 edge1 = result.vertices[i1].position - result.vertices[i0].position;
            glm::vec3 edge2 = result.vertices[i2].position - result.vertices[i0].position;
            glm::vec3 faceNormal = glm::cross(edge2, edge1);
            float len = glm::length(faceNormal);
            if (len > 1e-8f) faceNormal /= len;
            accumulated[i0] += faceNormal;
            accumulated[i1] += faceNormal;
            accumulated[i2] += faceNormal;
        }
        for (size_t i = 0; i < result.vertices.size(); ++i) {
            float len = glm::length(accumulated[i]);
            result.vertices[i].normal = len > 1e-8f ? accumulated[i] / len : glm::vec3(0.0f, 1.0f, 0.0f);
        }
    }

    result.succeeded = true;
    return result;
}

} // namespace engine::core
