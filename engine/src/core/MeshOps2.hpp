#pragma once

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <tuple>
#include <vector>

#include "core/MeshOps.hpp"

namespace engine::core::meshops {

// Merges vertices closer than `distance` (grid snapping). Triangles that collapse are removed.
inline MeshData weld(const MeshData& mesh, float distance) {
    MeshData out;
    std::map<std::tuple<long, long, long>, uint32_t> grid;
    std::vector<uint32_t> remap(mesh.positions.size());
    const float cell = std::max(distance, 1e-9f);
    for (std::size_t i = 0; i < mesh.positions.size(); ++i) {
        const Vec3& p = mesh.positions[i];
        const auto key = std::make_tuple(std::lround(p[0] / cell), std::lround(p[1] / cell), std::lround(p[2] / cell));
        auto it = grid.find(key);
        if (it == grid.end()) {
            it = grid.emplace(key, static_cast<uint32_t>(out.positions.size())).first;
            out.positions.push_back(p);
        }
        remap[i] = it->second;
    }
    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const uint32_t a = remap[mesh.indices[i]], b = remap[mesh.indices[i + 1]], c = remap[mesh.indices[i + 2]];
        if (a != b && b != c && a != c) out.indices.insert(out.indices.end(), {a, b, c});
    }
    return out;
}

// Mirrors across the plane axis=0 (axis 0,1,2 = X,Y,Z), keeping the original and adding a flipped copy.
inline MeshData mirror(const MeshData& mesh, int axis) {
    MeshData out = mesh;
    const uint32_t base = static_cast<uint32_t>(mesh.positions.size());
    for (Vec3 p : mesh.positions) {
        p[axis] = -p[axis];
        out.positions.push_back(p);
    }
    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        out.indices.insert(out.indices.end(), {base + mesh.indices[i], base + mesh.indices[i + 2], base + mesh.indices[i + 1]});
    }
    return out;
}

inline MeshData join(const MeshData& a, const MeshData& b) {
    MeshData out = a;
    const uint32_t base = static_cast<uint32_t>(a.positions.size());
    out.positions.insert(out.positions.end(), b.positions.begin(), b.positions.end());
    for (uint32_t i : b.indices) out.indices.push_back(base + i);
    return out;
}

// Copies of the mesh offset by `step`, `count` times in total.
inline MeshData arrayDuplicate(const MeshData& mesh, int count, Vec3 step) {
    MeshData out;
    for (int i = 0; i < count; ++i) {
        MeshData copy = mesh;
        for (Vec3& p : copy.positions) {
            for (int k = 0; k < 3; ++k) p[k] += step[k] * static_cast<float>(i);
        }
        out = i == 0 ? copy : join(out, copy);
    }
    return out;
}

// Copies rotated around the Y axis at equal angles.
inline MeshData radialDuplicate(const MeshData& mesh, int count) {
    MeshData out;
    for (int i = 0; i < count; ++i) {
        const float a = 6.2831853f * static_cast<float>(i) / static_cast<float>(std::max(count, 1));
        const float c = std::cos(a), s = std::sin(a);
        MeshData copy = mesh;
        for (Vec3& p : copy.positions) p = {p[0] * c + p[2] * s, p[1], -p[0] * s + p[2] * c};
        out = i == 0 ? copy : join(out, copy);
    }
    return out;
}

struct MeshValidation {
    int boundaryEdges = 0;     // used by 1 triangle (holes)
    int nonManifoldEdges = 0;  // used by 3 or more triangles
    int degenerateTriangles = 0;
    int badIndices = 0;
    [[nodiscard]] bool clean() const { return !boundaryEdges && !nonManifoldEdges && !degenerateTriangles && !badIndices; }
};

inline MeshValidation validate(const MeshData& mesh) {
    MeshValidation v;
    std::map<std::pair<uint32_t, uint32_t>, int> edges;
    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const uint32_t t[3] = {mesh.indices[i], mesh.indices[i + 1], mesh.indices[i + 2]};
        if (t[0] >= mesh.positions.size() || t[1] >= mesh.positions.size() || t[2] >= mesh.positions.size()) {
            ++v.badIndices;
            continue;
        }
        const Vec3 n = cross(sub(mesh.positions[t[1]], mesh.positions[t[0]]), sub(mesh.positions[t[2]], mesh.positions[t[0]]));
        if (n[0] * n[0] + n[1] * n[1] + n[2] * n[2] < 1e-18f) ++v.degenerateTriangles;
        for (int e = 0; e < 3; ++e) {
            uint32_t a = t[e], b = t[(e + 1) % 3];
            if (a > b) std::swap(a, b);
            ++edges[{a, b}];
        }
    }
    for (const auto& kv : edges) {
        if (kv.second == 1) ++v.boundaryEdges;
        if (kv.second >= 3) ++v.nonManifoldEdges;
    }
    return v;
}

// Connected pieces of the mesh: island id for every triangle.
inline std::vector<int> islands(const MeshData& mesh, int& count) {
    const std::size_t verts = mesh.positions.size();
    std::vector<std::size_t> parent(verts);
    for (std::size_t i = 0; i < verts; ++i) parent[i] = i;
    auto find = [&](std::size_t x) {
        while (parent[x] != x) x = parent[x] = parent[parent[x]];
        return x;
    };
    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        for (int k = 1; k < 3; ++k) {
            if (mesh.indices[i] < verts && mesh.indices[i + k] < verts) parent[find(mesh.indices[i + k])] = find(mesh.indices[i]);
        }
    }
    std::map<std::size_t, int> ids;
    std::vector<int> out;
    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const std::size_t root = find(std::min<std::size_t>(mesh.indices[i], verts - 1));
        auto it = ids.find(root);
        if (it == ids.end()) it = ids.emplace(root, static_cast<int>(ids.size())).first;
        out.push_back(it->second);
    }
    count = static_cast<int>(ids.size());
    return out;
}

// Linear subdivision: every triangle becomes four (new vertices at edge midpoints; no smoothing).
inline MeshData subdivideLinear(const MeshData& mesh) {
    MeshData out;
    out.positions = mesh.positions;
    std::map<std::pair<uint32_t, uint32_t>, uint32_t> mid;
    auto midpoint = [&](uint32_t a, uint32_t b) {
        if (a > b) std::swap(a, b);
        auto it = mid.find({a, b});
        if (it != mid.end()) return it->second;
        const Vec3& p = out.positions[a];
        const Vec3& q = out.positions[b];
        out.positions.push_back({(p[0] + q[0]) * 0.5f, (p[1] + q[1]) * 0.5f, (p[2] + q[2]) * 0.5f});
        return mid[{a, b}] = static_cast<uint32_t>(out.positions.size() - 1);
    };
    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const uint32_t a = mesh.indices[i], b = mesh.indices[i + 1], c = mesh.indices[i + 2];
        const uint32_t ab = midpoint(a, b), bc = midpoint(b, c), ca = midpoint(c, a);
        out.indices.insert(out.indices.end(), {a, ab, ca, ab, b, bc, ca, bc, c, ab, bc, ca});
    }
    return out;
}

// Bakes a uniform scale and translation into the vertices (export with a clean transform).
inline MeshData applyTransform(const MeshData& mesh, float scale, Vec3 translate) {
    MeshData out = mesh;
    for (Vec3& p : out.positions) {
        for (int k = 0; k < 3; ++k) p[k] = p[k] * scale + translate[k];
    }
    return out;
}

// Edges that are seams: shared by two triangles whose UV indices for that edge differ.
inline std::vector<std::pair<uint32_t, uint32_t>> uvSeams(const std::vector<uint32_t>& indices, const std::vector<uint32_t>& uvIndices) {
    std::map<std::pair<uint32_t, uint32_t>, std::pair<std::pair<uint32_t, uint32_t>, bool>> seen;
    std::set<std::pair<uint32_t, uint32_t>> seams;
    for (std::size_t i = 0; i + 2 < indices.size() && i + 2 < uvIndices.size(); i += 3) {
        for (int e = 0; e < 3; ++e) {
            const uint32_t a = indices[i + e], b = indices[i + (e + 1) % 3];
            const uint32_t ua = uvIndices[i + e], ub = uvIndices[i + (e + 1) % 3];
            const auto key = std::make_pair(std::min(a, b), std::max(a, b));
            const auto uv = a < b ? std::make_pair(ua, ub) : std::make_pair(ub, ua);
            auto it = seen.find(key);
            if (it == seen.end()) seen[key] = {uv, true};
            else if (it->second.first != uv) seams.insert(key);
        }
    }
    return {seams.begin(), seams.end()};
}

// Simple LOD: snap vertices to a coarser grid and weld. `factor` > 1 reduces more.
inline MeshData simplifyByGrid(const MeshData& mesh, float cellSize) { return weld(mesh, cellSize); }

} // namespace engine::core::meshops
