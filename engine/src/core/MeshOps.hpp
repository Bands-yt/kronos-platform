#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace engine::core::meshops {

using Vec3 = std::array<float, 3>;

struct MeshData {
    std::vector<Vec3> positions;
    std::vector<uint32_t> indices; // triangles
};

inline Vec3 sub(const Vec3& a, const Vec3& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
inline Vec3 normalized(const Vec3& v) {
    const float len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    return len > 1e-12f ? Vec3{v[0] / len, v[1] / len, v[2] / len} : Vec3{0.0f, 1.0f, 0.0f};
}

// Smooth vertex normals: face normals weighted by area, summed per vertex.
inline std::vector<Vec3> recalculateNormals(const MeshData& mesh) {
    std::vector<Vec3> normals(mesh.positions.size(), Vec3{0.0f, 0.0f, 0.0f});
    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const uint32_t a = mesh.indices[i], b = mesh.indices[i + 1], c = mesh.indices[i + 2];
        if (a >= normals.size() || b >= normals.size() || c >= normals.size()) continue;
        const Vec3 n = cross(sub(mesh.positions[b], mesh.positions[a]), sub(mesh.positions[c], mesh.positions[a]));
        for (uint32_t v : {a, b, c}) {
            for (int k = 0; k < 3; ++k) normals[v][k] += n[k];
        }
    }
    for (auto& n : normals) n = normalized(n);
    return normals;
}

// Adds an inner copy pushed along the (reversed) normals, with flipped winding.
inline MeshData solidify(const MeshData& mesh, float thickness) {
    MeshData out = mesh;
    const std::vector<Vec3> normals = recalculateNormals(mesh);
    const uint32_t base = static_cast<uint32_t>(mesh.positions.size());
    for (std::size_t i = 0; i < mesh.positions.size(); ++i) {
        out.positions.push_back({mesh.positions[i][0] - normals[i][0] * thickness,
                                 mesh.positions[i][1] - normals[i][1] * thickness,
                                 mesh.positions[i][2] - normals[i][2] * thickness});
    }
    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        out.indices.push_back(base + mesh.indices[i]);
        out.indices.push_back(base + mesh.indices[i + 2]);
        out.indices.push_back(base + mesh.indices[i + 1]);
    }
    return out;
}

struct Bounds {
    Vec3 min{0.0f, 0.0f, 0.0f};
    Vec3 max{0.0f, 0.0f, 0.0f};
    [[nodiscard]] Vec3 center() const {
        return {(min[0] + max[0]) * 0.5f, (min[1] + max[1]) * 0.5f, (min[2] + max[2]) * 0.5f};
    }
};

inline Bounds bounds(const MeshData& mesh) {
    Bounds b;
    if (mesh.positions.empty()) return b;
    b.min = b.max = mesh.positions.front();
    for (const Vec3& p : mesh.positions) {
        for (int k = 0; k < 3; ++k) {
            b.min[k] = std::min(b.min[k], p[k]);
            b.max[k] = std::max(b.max[k], p[k]);
        }
    }
    return b;
}

enum class Pivot { BoundsCenter, BoundsBottom, Custom };

// Moves the vertices so the chosen pivot sits at the origin. Returns the offset that was removed.
inline Vec3 recenter(MeshData& mesh, Pivot pivot, Vec3 custom = {0.0f, 0.0f, 0.0f}) {
    const Bounds b = bounds(mesh);
    Vec3 offset = custom;
    if (pivot == Pivot::BoundsCenter) offset = b.center();
    if (pivot == Pivot::BoundsBottom) {
        offset = b.center();
        offset[1] = b.min[1];
    }
    for (Vec3& p : mesh.positions) p = sub(p, offset);
    return offset;
}

struct MeshDiff {
    bool sameTopology = false;
    int vertexDelta = 0;
    int triangleDelta = 0;
    float maxVertexDistance = 0.0f; // only when topology matches
    int movedVertices = 0;
};

inline MeshDiff compare(const MeshData& a, const MeshData& b, float tolerance = 1e-5f) {
    MeshDiff d;
    d.vertexDelta = static_cast<int>(b.positions.size()) - static_cast<int>(a.positions.size());
    d.triangleDelta = static_cast<int>(b.indices.size() / 3) - static_cast<int>(a.indices.size() / 3);
    d.sameTopology = d.vertexDelta == 0 && a.indices == b.indices;
    if (d.vertexDelta == 0) {
        for (std::size_t i = 0; i < a.positions.size(); ++i) {
            const Vec3 v = sub(b.positions[i], a.positions[i]);
            const float dist = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
            d.maxVertexDistance = std::max(d.maxVertexDistance, dist);
            if (dist > tolerance) ++d.movedVertices;
        }
    }
    return d;
}

} // namespace engine::core::meshops
