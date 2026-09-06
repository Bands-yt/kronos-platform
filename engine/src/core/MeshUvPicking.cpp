#include "core/MeshUvPicking.hpp"

#include <array>
#include <cmath>
#include <cstdint>

#include "core/EditableMesh.hpp"

namespace engine::core {

namespace {

constexpr float kEpsilon = 1e-7f;

// Real Moller-Trumbore ray-triangle intersection. Returns true and
// fills outT/outU/outV (barycentric -- the hit point is
// (1-outU-outV)*v0 + outU*v1 + outV*v2) on a real hit within
// [0, maxDistance] along `direction` (not required to be normalized;
// outT is then a real distance in `direction`'s own units, matching
// this function's one real caller's convention of narrowing
// `maxDistance` to the closest hit found so far as it scans faces).
bool intersectRayTriangle(glm::vec3 origin, glm::vec3 direction, glm::vec3 v0, glm::vec3 v1, glm::vec3 v2,
                          float maxDistance, float& outT, float& outU, float& outV) {
    glm::vec3 edge1 = v1 - v0;
    glm::vec3 edge2 = v2 - v0;
    glm::vec3 pvec = glm::cross(direction, edge2);
    float det = glm::dot(edge1, pvec);
    if (std::fabs(det) < kEpsilon) return false; // ray parallel to the triangle's own plane
    float invDet = 1.0f / det;

    glm::vec3 tvec = origin - v0;
    float u = glm::dot(tvec, pvec) * invDet;
    if (u < 0.0f || u > 1.0f) return false;

    glm::vec3 qvec = glm::cross(tvec, edge1);
    float v = glm::dot(direction, qvec) * invDet;
    if (v < 0.0f || u + v > 1.0f) return false;

    float t = glm::dot(edge2, qvec) * invDet;
    if (t < 0.0f || t > maxDistance) return false;

    outT = t;
    outU = u;
    outV = v;
    return true;
}

} // namespace

MeshUvPickResult pickTriangleUv(const EditableMesh& mesh, glm::vec3 origin, glm::vec3 direction, float maxDistance) {
    MeshUvPickResult result;
    float closestT = maxDistance;

    const std::vector<Vertex>& vertices = mesh.vertices();
    size_t faces = mesh.faceCount();
    for (size_t face = 0; face < faces; ++face) {
        std::array<uint32_t, 3> idx = mesh.faceVertexIndices(face);
        const Vertex& a = vertices[idx[0]];
        const Vertex& b = vertices[idx[1]];
        const Vertex& c = vertices[idx[2]];

        float t = 0.0f, u = 0.0f, v = 0.0f;
        if (!intersectRayTriangle(origin, direction, a.position, b.position, c.position, closestT, t, u, v)) continue;

        closestT = t; // every later face only needs to beat this real closest hit so far
        result.hit = true;
        result.faceIndex = face;
        result.distance = t;
        result.point = origin + direction * t;
        float w = 1.0f - u - v;
        result.uv = w * a.uv + u * b.uv + v * c.uv;
        result.normal = glm::normalize(w * a.normal + u * b.normal + v * c.normal);
    }
    return result;
}

} // namespace engine::core
