#include "core/EditableMesh.hpp"

#include <algorithm>
#include <map>
#include <set>

namespace engine::core {

EditableMesh EditableMesh::fromVertexData(std::vector<Vertex> vertices, std::vector<uint32_t> indices) {
    EditableMesh mesh;
    mesh.vertices_ = std::move(vertices);
    mesh.indices_ = std::move(indices);
    return mesh;
}

EditableMesh EditableMesh::createBox(glm::vec3 h, glm::vec3 center) {
    // Same 24-vertex (4 per face x 6 faces), flat-per-face-normal layout
    // Mesh::createBox() uploads -- kept here as real, CPU-retained data
    // instead of going straight to the GPU. See Mesh.cpp's own
    // createBox() for the identical vertex list this mirrors.
    std::vector<Vertex> vertices = {
        {{h.x, -h.y, -h.z}, {1, 0, 0}, {0, 0}}, {{h.x, -h.y, h.z}, {1, 0, 0}, {1, 0}},
        {{h.x, h.y, h.z}, {1, 0, 0}, {1, 1}},   {{h.x, h.y, -h.z}, {1, 0, 0}, {0, 1}},
        {{-h.x, -h.y, h.z}, {-1, 0, 0}, {0, 0}}, {{-h.x, -h.y, -h.z}, {-1, 0, 0}, {1, 0}},
        {{-h.x, h.y, -h.z}, {-1, 0, 0}, {1, 1}}, {{-h.x, h.y, h.z}, {-1, 0, 0}, {0, 1}},
        {{-h.x, h.y, -h.z}, {0, 1, 0}, {0, 0}}, {{h.x, h.y, -h.z}, {0, 1, 0}, {1, 0}},
        {{h.x, h.y, h.z}, {0, 1, 0}, {1, 1}},   {{-h.x, h.y, h.z}, {0, 1, 0}, {0, 1}},
        {{-h.x, -h.y, h.z}, {0, -1, 0}, {0, 0}}, {{h.x, -h.y, h.z}, {0, -1, 0}, {1, 0}},
        {{h.x, -h.y, -h.z}, {0, -1, 0}, {1, 1}}, {{-h.x, -h.y, -h.z}, {0, -1, 0}, {0, 1}},
        {{h.x, -h.y, h.z}, {0, 0, 1}, {0, 0}},  {{-h.x, -h.y, h.z}, {0, 0, 1}, {1, 0}},
        {{-h.x, h.y, h.z}, {0, 0, 1}, {1, 1}},  {{h.x, h.y, h.z}, {0, 0, 1}, {0, 1}},
        {{-h.x, -h.y, -h.z}, {0, 0, -1}, {0, 0}}, {{h.x, -h.y, -h.z}, {0, 0, -1}, {1, 0}},
        {{h.x, h.y, -h.z}, {0, 0, -1}, {1, 1}}, {{-h.x, h.y, -h.z}, {0, 0, -1}, {0, 1}},
    };
    if (center != glm::vec3(0.0f)) {
        for (Vertex& v : vertices) v.position += center;
    }
    std::vector<uint32_t> indices;
    indices.reserve(36);
    for (uint32_t face = 0; face < 6; ++face) {
        uint32_t base = face * 4;
        indices.insert(indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }
    return fromVertexData(std::move(vertices), std::move(indices));
}

glm::vec3 EditableMesh::boundsMin() const {
    if (vertices_.empty()) return glm::vec3(0.0f);
    glm::vec3 result = vertices_[0].position;
    for (const Vertex& v : vertices_) result = glm::min(result, v.position);
    return result;
}

glm::vec3 EditableMesh::boundsMax() const {
    if (vertices_.empty()) return glm::vec3(0.0f);
    glm::vec3 result = vertices_[0].position;
    for (const Vertex& v : vertices_) result = glm::max(result, v.position);
    return result;
}

std::array<uint32_t, 3> EditableMesh::faceVertexIndices(size_t faceIndex) const {
    if (faceIndex >= faceCount()) return {0, 0, 0};
    return {indices_[faceIndex * 3], indices_[faceIndex * 3 + 1], indices_[faceIndex * 3 + 2]};
}

glm::vec3 EditableMesh::faceCentroid(size_t faceIndex) const {
    auto [a, b, c] = faceVertexIndices(faceIndex);
    return (vertices_[a].position + vertices_[b].position + vertices_[c].position) / 3.0f;
}

glm::vec3 EditableMesh::faceNormal(size_t faceIndex) const {
    auto [a, b, c] = faceVertexIndices(faceIndex);
    glm::vec3 edge1 = vertices_[b].position - vertices_[a].position;
    glm::vec3 edge2 = vertices_[c].position - vertices_[a].position;
    glm::vec3 normal = glm::cross(edge1, edge2);
    float len = glm::length(normal);
    return len > 1e-8f ? normal / len : glm::vec3(0.0f, 1.0f, 0.0f);
}

std::vector<std::pair<uint32_t, uint32_t>> EditableMesh::allEdges() const {
    std::set<std::pair<uint32_t, uint32_t>> unique;
    for (size_t f = 0; f < faceCount(); ++f) {
        auto [a, b, c] = faceVertexIndices(f);
        uint32_t vs[3] = {a, b, c};
        for (int i = 0; i < 3; ++i) {
            uint32_t v0 = vs[i], v1 = vs[(i + 1) % 3];
            unique.insert({std::min(v0, v1), std::max(v0, v1)});
        }
    }
    return std::vector<std::pair<uint32_t, uint32_t>>(unique.begin(), unique.end());
}

bool EditableMesh::extrudeFace(size_t faceIndex, float distance) {
    if (faceIndex >= faceCount()) return false;
    auto [a, b, c] = faceVertexIndices(faceIndex);
    glm::vec3 normal = faceNormal(faceIndex);
    glm::vec3 offset = normal * distance;

    Vertex va = vertices_[a], vb = vertices_[b], vc = vertices_[c];
    va.position += offset;
    vb.position += offset;
    vc.position += offset;

    uint32_t na = static_cast<uint32_t>(vertices_.size());
    vertices_.push_back(va);
    uint32_t nb = static_cast<uint32_t>(vertices_.size());
    vertices_.push_back(vb);
    uint32_t nc = static_cast<uint32_t>(vertices_.size());
    vertices_.push_back(vc);

    // The cap moves to the extruded position -- same winding, new
    // (offset) vertices.
    indices_[faceIndex * 3 + 0] = na;
    indices_[faceIndex * 3 + 1] = nb;
    indices_[faceIndex * 3 + 2] = nc;

    // 3 real quad walls (2 triangles each) connecting the original edge
    // loop to the new one, wound to face outward (consistent with the
    // original face's own a->b->c winding).
    auto appendWall = [this](uint32_t o0, uint32_t o1, uint32_t n0, uint32_t n1) {
        indices_.insert(indices_.end(), {o0, o1, n1, o0, n1, n0});
    };
    appendWall(a, b, na, nb);
    appendWall(b, c, nb, nc);
    appendWall(c, a, nc, na);
    return true;
}

bool EditableMesh::subdivideFace(size_t faceIndex) {
    if (faceIndex >= faceCount()) return false;
    auto [a, b, c] = faceVertexIndices(faceIndex);

    auto midpoint = [this](uint32_t i0, uint32_t i1) -> uint32_t {
        Vertex mid;
        mid.position = (vertices_[i0].position + vertices_[i1].position) * 0.5f;
        glm::vec3 n = vertices_[i0].normal + vertices_[i1].normal;
        float len = glm::length(n);
        mid.normal = len > 1e-8f ? n / len : vertices_[i0].normal;
        mid.uv = (vertices_[i0].uv + vertices_[i1].uv) * 0.5f;
        uint32_t index = static_cast<uint32_t>(vertices_.size());
        vertices_.push_back(mid);
        return index;
    };

    uint32_t mab = midpoint(a, b);
    uint32_t mbc = midpoint(b, c);
    uint32_t mca = midpoint(c, a);

    // Real 1-to-4 split -- replace the original triangle with the first
    // of the 4, append the other 3.
    indices_[faceIndex * 3 + 0] = a;
    indices_[faceIndex * 3 + 1] = mab;
    indices_[faceIndex * 3 + 2] = mca;
    indices_.insert(indices_.end(), {mab, b, mbc, mca, mbc, c, mab, mbc, mca});
    return true;
}

size_t EditableMesh::mergeVertices(float distanceThreshold) {
    size_t n = vertices_.size();
    std::vector<uint32_t> remap(n);
    for (uint32_t i = 0; i < n; ++i) remap[i] = i;

    // Real, direct O(n^2) pairwise weld -- Block Builder-sized meshes
    // (tens to low hundreds of vertices after a few edits) never make
    // this a real cost; a spatial hash would be premature here.
    for (size_t i = 0; i < n; ++i) {
        if (remap[i] != i) continue; // already merged into an earlier vertex
        for (size_t j = i + 1; j < n; ++j) {
            if (remap[j] != j) continue;
            if (glm::distance(vertices_[i].position, vertices_[j].position) < distanceThreshold) {
                remap[j] = static_cast<uint32_t>(i);
            }
        }
    }

    size_t mergedCount = 0;
    for (size_t i = 0; i < n; ++i) {
        if (remap[i] != i) ++mergedCount;
    }
    if (mergedCount == 0) return 0;

    for (uint32_t& index : indices_) index = remap[index];

    // Drop now-degenerate faces (two or more indices equal after remap).
    std::vector<uint32_t> keptIndices;
    keptIndices.reserve(indices_.size());
    for (size_t f = 0; f * 3 < indices_.size(); ++f) {
        uint32_t a = indices_[f * 3], b = indices_[f * 3 + 1], c = indices_[f * 3 + 2];
        if (a != b && b != c && a != c) keptIndices.insert(keptIndices.end(), {a, b, c});
    }
    indices_ = std::move(keptIndices);

    // Compact: drop vertices no longer referenced by any real face, and
    // remap indices_ down to the new, dense vertex list.
    std::vector<bool> referenced(n, false);
    for (uint32_t index : indices_) referenced[index] = true;
    std::vector<uint32_t> compactRemap(n, 0);
    std::vector<Vertex> compactVertices;
    for (size_t i = 0; i < n; ++i) {
        if (!referenced[i]) continue;
        compactRemap[i] = static_cast<uint32_t>(compactVertices.size());
        compactVertices.push_back(vertices_[i]);
    }
    for (uint32_t& index : indices_) index = compactRemap[index];
    vertices_ = std::move(compactVertices);

    return mergedCount;
}

bool EditableMesh::bevelEdge(uint32_t v0, uint32_t v1, float amount) {
    // Find every face containing both v0 and v1 -- a real interior edge
    // has exactly 2; anything else is a real, honest no-op (see class
    // comment).
    std::vector<size_t> adjacentFaces;
    for (size_t f = 0; f < faceCount(); ++f) {
        auto [a, b, c] = faceVertexIndices(f);
        bool hasV0 = (a == v0 || b == v0 || c == v0);
        bool hasV1 = (a == v1 || b == v1 || c == v1);
        if (hasV0 && hasV1) adjacentFaces.push_back(f);
    }
    if (adjacentFaces.size() != 2) return false;

    amount = std::clamp(amount, 0.0f, 1.0f);

    auto opposite = [this](size_t faceIndex, uint32_t a, uint32_t b) -> uint32_t {
        auto [x, y, z] = faceVertexIndices(faceIndex);
        if (x != a && x != b) return x;
        if (y != a && y != b) return y;
        return z;
    };

    size_t faceA = adjacentFaces[0], faceB = adjacentFaces[1];
    uint32_t oppA = opposite(faceA, v0, v1);
    uint32_t oppB = opposite(faceB, v0, v1);

    auto offsetToward = [this](uint32_t from, uint32_t toward, float t) -> Vertex {
        Vertex v = vertices_[from];
        v.position = glm::mix(vertices_[from].position, vertices_[toward].position, t);
        return v;
    };

    uint32_t v0a = static_cast<uint32_t>(vertices_.size());
    vertices_.push_back(offsetToward(v0, oppA, amount));
    uint32_t v1a = static_cast<uint32_t>(vertices_.size());
    vertices_.push_back(offsetToward(v1, oppA, amount));
    uint32_t v0b = static_cast<uint32_t>(vertices_.size());
    vertices_.push_back(offsetToward(v0, oppB, amount));
    uint32_t v1b = static_cast<uint32_t>(vertices_.size());
    vertices_.push_back(offsetToward(v1, oppB, amount));

    // Rewrite faceA/faceB's own v0/v1 references to the new, offset
    // vertices -- the shared original edge no longer exists as a single
    // line, replaced by the new connecting quad below.
    auto rewriteFace = [this](size_t faceIndex, uint32_t oldV0, uint32_t oldV1, uint32_t newV0, uint32_t newV1) {
        for (int i = 0; i < 3; ++i) {
            uint32_t& idx = indices_[faceIndex * 3 + i];
            if (idx == oldV0) idx = newV0;
            else if (idx == oldV1) idx = newV1;
        }
    };
    rewriteFace(faceA, v0, v1, v0a, v1a);
    rewriteFace(faceB, v0, v1, v0b, v1b);

    // New connecting quad filling the gap between the two offset edges.
    indices_.insert(indices_.end(), {v0a, v1a, v1b, v0a, v1b, v0b});
    return true;
}

bool EditableMesh::insetFace(size_t faceIndex, float amount) {
    if (faceIndex >= faceCount()) return false;
    amount = std::clamp(amount, 0.0f, 1.0f);
    auto [a, b, c] = faceVertexIndices(faceIndex);
    glm::vec3 centroid = faceCentroid(faceIndex);

    auto insetVertex = [&](uint32_t original) -> uint32_t {
        Vertex v = vertices_[original];
        v.position = glm::mix(vertices_[original].position, centroid, amount);
        uint32_t index = static_cast<uint32_t>(vertices_.size());
        vertices_.push_back(v);
        return index;
    };
    uint32_t ia = insetVertex(a);
    uint32_t ib = insetVertex(b);
    uint32_t ic = insetVertex(c);

    indices_[faceIndex * 3 + 0] = ia;
    indices_[faceIndex * 3 + 1] = ib;
    indices_[faceIndex * 3 + 2] = ic;

    auto appendWall = [this](uint32_t o0, uint32_t o1, uint32_t n0, uint32_t n1) {
        indices_.insert(indices_.end(), {o0, o1, n1, o0, n1, n0});
    };
    appendWall(a, b, ia, ib);
    appendWall(b, c, ib, ic);
    appendWall(c, a, ic, ia);
    return true;
}

} // namespace engine::core
