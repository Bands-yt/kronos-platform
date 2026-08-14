#include "core/UvTools.hpp"

#include <algorithm>
#include <cmath>

namespace engine::core {

void applyPlanarProjection(EditableMesh& mesh, ProjectionAxis axis) {
    glm::vec3 boundsMin = mesh.boundsMin();
    glm::vec3 size = glm::max(mesh.boundsMax() - boundsMin, glm::vec3(1e-6f));

    for (uint32_t i = 0; i < static_cast<uint32_t>(mesh.vertexCount()); ++i) {
        glm::vec3 pos = mesh.vertices()[i].position;
        glm::vec2 uv;
        switch (axis) {
            case ProjectionAxis::X: uv = {(pos.y - boundsMin.y) / size.y, (pos.z - boundsMin.z) / size.z}; break;
            case ProjectionAxis::Y: uv = {(pos.x - boundsMin.x) / size.x, (pos.z - boundsMin.z) / size.z}; break;
            case ProjectionAxis::Z: uv = {(pos.x - boundsMin.x) / size.x, (pos.y - boundsMin.y) / size.y}; break;
        }
        mesh.setVertexUv(i, uv);
    }
}

void applyCubeProjection(EditableMesh& mesh) {
    glm::vec3 boundsMin = mesh.boundsMin();
    glm::vec3 size = glm::max(mesh.boundsMax() - boundsMin, glm::vec3(1e-6f));

    for (size_t f = 0; f < mesh.faceCount(); ++f) {
        glm::vec3 normal = mesh.faceNormal(f);
        glm::vec3 absNormal = glm::abs(normal);
        auto indices = mesh.faceVertexIndices(f);
        for (uint32_t vi : indices) {
            glm::vec3 pos = mesh.vertices()[vi].position;
            glm::vec2 uv;
            if (absNormal.x >= absNormal.y && absNormal.x >= absNormal.z) {
                uv = {(pos.y - boundsMin.y) / size.y, (pos.z - boundsMin.z) / size.z};
            } else if (absNormal.y >= absNormal.x && absNormal.y >= absNormal.z) {
                uv = {(pos.x - boundsMin.x) / size.x, (pos.z - boundsMin.z) / size.z};
            } else {
                uv = {(pos.x - boundsMin.x) / size.x, (pos.y - boundsMin.y) / size.y};
            }
            mesh.setVertexUv(vi, uv);
        }
    }
}

EditableMesh applyAutoUnwrap(const EditableMesh& mesh) {
    size_t faceCount = mesh.faceCount();
    std::vector<Vertex> newVertices;
    std::vector<uint32_t> newIndices;
    if (faceCount == 0) return EditableMesh::fromVertexData(std::move(newVertices), std::move(newIndices));
    newVertices.reserve(faceCount * 3);
    newIndices.reserve(faceCount * 3);

    int gridSize = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(faceCount))));
    float cell = 1.0f / static_cast<float>(gridSize);
    float margin = cell * 0.05f;
    float usable = cell - margin * 2.0f;

    for (size_t f = 0; f < faceCount; ++f) {
        auto indices = mesh.faceVertexIndices(f);
        Vertex va = mesh.vertices()[indices[0]];
        Vertex vb = mesh.vertices()[indices[1]];
        Vertex vc = mesh.vertices()[indices[2]];

        float lenAb = glm::length(vb.position - va.position);
        float lenAc = glm::length(vc.position - va.position);
        float lenBc = glm::length(vc.position - vb.position);

        // Real, shape-preserving layout: a at the local origin, b along
        // +U at its real distance, c placed via the law of cosines so
        // all 3 real edge lengths are preserved exactly.
        float angleA = 0.0f;
        if (lenAb > 1e-6f && lenAc > 1e-6f) {
            float cosA = (lenAb * lenAb + lenAc * lenAc - lenBc * lenBc) / (2.0f * lenAb * lenAc);
            angleA = std::acos(std::clamp(cosA, -1.0f, 1.0f));
        }
        glm::vec2 localA(0.0f, 0.0f);
        glm::vec2 localB(lenAb, 0.0f);
        glm::vec2 localC(lenAc * std::cos(angleA), lenAc * std::sin(angleA));

        glm::vec2 localMin = glm::min(glm::min(localA, localB), localC);
        glm::vec2 localMax = glm::max(glm::max(localA, localB), localC);
        glm::vec2 localSize = glm::max(localMax - localMin, glm::vec2(1e-6f));
        float scale = usable / std::max(localSize.x, localSize.y);

        int col = static_cast<int>(f) % gridSize;
        int row = static_cast<int>(f) / gridSize;
        glm::vec2 cellOrigin(static_cast<float>(col) * cell + margin, static_cast<float>(row) * cell + margin);

        va.uv = cellOrigin + (localA - localMin) * scale;
        vb.uv = cellOrigin + (localB - localMin) * scale;
        vc.uv = cellOrigin + (localC - localMin) * scale;

        // Each triangle gets its own 3 fresh vertices -- no sharing, so
        // no later triangle can ever overwrite an earlier one's UV (see
        // this function's own header comment for why that's required,
        // not just simpler).
        uint32_t baseIndex = static_cast<uint32_t>(newVertices.size());
        newVertices.push_back(va);
        newVertices.push_back(vb);
        newVertices.push_back(vc);
        newIndices.insert(newIndices.end(), {baseIndex, baseIndex + 1, baseIndex + 2});
    }

    return EditableMesh::fromVertexData(std::move(newVertices), std::move(newIndices));
}

} // namespace engine::core
