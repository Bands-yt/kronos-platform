#include "brokenbones/RockMesh.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <random>

namespace engine::brokenbones {

namespace {

uint32_t hash3(int x, int y, int z, uint32_t seed) {
    uint32_t h = seed * 0x9E3779B1u;
    h ^= static_cast<uint32_t>(x) * 0x85EBCA77u;
    h = (h << 13) | (h >> 19);
    h ^= static_cast<uint32_t>(y) * 0xC2B2AE3Du;
    h = (h << 13) | (h >> 19);
    h ^= static_cast<uint32_t>(z) * 0x27D4EB2Fu;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    return h;
}

float lattice(int x, int y, int z, uint32_t seed) {
    return static_cast<float>(hash3(x, y, z, seed) & 0xFFFFFFu) / static_cast<float>(0xFFFFFF) * 2.0f - 1.0f;
}

float valueNoise3D(glm::vec3 p, uint32_t seed) {
    glm::vec3 cell = glm::floor(p);
    glm::vec3 f = p - cell;
    glm::vec3 u = f * f * (3.0f - 2.0f * f);
    int x = static_cast<int>(cell.x);
    int y = static_cast<int>(cell.y);
    int z = static_cast<int>(cell.z);
    float c000 = lattice(x, y, z, seed), c100 = lattice(x + 1, y, z, seed);
    float c010 = lattice(x, y + 1, z, seed), c110 = lattice(x + 1, y + 1, z, seed);
    float c001 = lattice(x, y, z + 1, seed), c101 = lattice(x + 1, y, z + 1, seed);
    float c011 = lattice(x, y + 1, z + 1, seed), c111 = lattice(x + 1, y + 1, z + 1, seed);
    float x00 = glm::mix(c000, c100, u.x), x10 = glm::mix(c010, c110, u.x);
    float x01 = glm::mix(c001, c101, u.x), x11 = glm::mix(c011, c111, u.x);
    return glm::mix(glm::mix(x00, x10, u.y), glm::mix(x01, x11, u.y), u.z);
}

void buildIcosphere(int subdivisions, std::vector<glm::vec3>& positions, std::vector<uint32_t>& indices) {
    const float t = (1.0f + std::sqrt(5.0f)) * 0.5f;
    positions = {{-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0}, {0, -1, t}, {0, 1, t},
                 {0, -1, -t}, {0, 1, -t}, {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}};
    for (auto& p : positions) p = glm::normalize(p);
    indices = {0, 11, 5, 0, 5, 1, 0, 1, 7, 0, 7, 10, 0, 10, 11, 1, 5, 9, 5, 11, 4, 11, 10, 2, 10, 7, 6, 7, 1, 8,
               3, 9, 4, 3, 4, 2, 3, 2, 6, 3, 6, 8, 3, 8, 9, 4, 9, 5, 2, 4, 11, 6, 2, 10, 8, 6, 7, 9, 8, 1};

    for (int s = 0; s < subdivisions; ++s) {
        std::map<std::pair<uint32_t, uint32_t>, uint32_t> midpoints;
        auto midpoint = [&](uint32_t a, uint32_t b) {
            std::pair<uint32_t, uint32_t> key = a < b ? std::make_pair(a, b) : std::make_pair(b, a);
            auto it = midpoints.find(key);
            if (it != midpoints.end()) return it->second;
            positions.push_back(glm::normalize(positions[a] + positions[b]));
            uint32_t index = static_cast<uint32_t>(positions.size() - 1);
            midpoints.emplace(key, index);
            return index;
        };
        std::vector<uint32_t> next;
        next.reserve(indices.size() * 4);
        for (size_t i = 0; i + 2 < indices.size(); i += 3) {
            uint32_t a = indices[i], b = indices[i + 1], c = indices[i + 2];
            uint32_t ab = midpoint(a, b), bc = midpoint(b, c), ca = midpoint(c, a);
            next.insert(next.end(), {a, ab, ca, b, bc, ab, c, ca, bc, ab, bc, ca});
        }
        indices.swap(next);
    }
}

} // namespace

float rockNoise3D(glm::vec3 p, uint32_t seed, int octaves) {
    float sum = 0.0f;
    float amplitude = 0.5f;
    float norm = 0.0f;
    for (int o = 0; o < octaves; ++o) {
        sum += amplitude * valueNoise3D(p, seed + static_cast<uint32_t>(o) * 131u);
        norm += amplitude;
        p *= 2.03f;
        amplitude *= 0.5f;
    }
    return norm > 0.0f ? sum / norm : 0.0f;
}

RockMeshData generateRockMesh(const RockParams& params) {
    std::vector<glm::vec3> unit;
    std::vector<uint32_t> indices;
    buildIcosphere(std::clamp(params.subdivisions, 0, 5), unit, indices);

    std::mt19937 rng(params.seed);
    std::uniform_real_distribution<float> signedUnit(-1.0f, 1.0f);
    std::uniform_real_distribution<float> cutDepth(0.62f, 0.86f);
    struct Cut {
        glm::vec3 normal;
        float offset;
    };
    std::vector<Cut> cuts;
    for (int i = 0; i < params.facetCuts; ++i) {
        glm::vec3 n(signedUnit(rng), signedUnit(rng) * 0.7f, signedUnit(rng));
        if (glm::length(n) < 1e-3f) n = glm::vec3(0.0f, 1.0f, 0.0f);
        cuts.push_back({glm::normalize(n), cutDepth(rng)});
    }
    // The flattest side faces down so rocks sit or embed convincingly.
    cuts.push_back({glm::vec3(0.0f, -1.0f, 0.0f), 0.55f});

    RockMeshData out;
    out.vertices.resize(unit.size());
    std::vector<float> displacement(unit.size());
    for (size_t i = 0; i < unit.size(); ++i) {
        glm::vec3 dir = unit[i];
        float d = 1.0f + params.roughness * rockNoise3D(dir * 1.8f, params.seed, 4) +
                  params.roughness * 0.35f * rockNoise3D(dir * 6.0f, params.seed ^ 0xA5A5u, 3);
        glm::vec3 p = dir * d;
        for (const Cut& cut : cuts) {
            float h = glm::dot(p, cut.normal) - cut.offset;
            if (h > 0.0f) p -= cut.normal * h;
        }
        displacement[i] = glm::length(p);
        out.vertices[i].position = p * params.radius * params.stretch;
    }

    std::vector<glm::vec3> normals(unit.size(), glm::vec3(0.0f));
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        const glm::vec3& a = out.vertices[indices[i]].position;
        const glm::vec3& b = out.vertices[indices[i + 1]].position;
        const glm::vec3& c = out.vertices[indices[i + 2]].position;
        glm::vec3 faceNormal = glm::cross(b - a, c - a);
        normals[indices[i]] += faceNormal;
        normals[indices[i + 1]] += faceNormal;
        normals[indices[i + 2]] += faceNormal;
    }

    const glm::vec3 lightStone(0.60f, 0.57f, 0.52f);
    const glm::vec3 darkStone(0.36f, 0.34f, 0.32f);
    for (size_t i = 0; i < out.vertices.size(); ++i) {
        core::Vertex& v = out.vertices[i];
        glm::vec3 n = normals[i];
        v.normal = glm::length(n) > 1e-8f ? glm::normalize(n) : unit[i];
        // Box projection keeps texel density even on every side.
        glm::vec3 an = glm::abs(v.normal);
        glm::vec3 p = v.position * 0.45f;
        v.uv = an.x > an.y && an.x > an.z ? glm::vec2(p.z, p.y) : an.y > an.z ? glm::vec2(p.x, p.z) : glm::vec2(p.x, p.y);
        float cavity = glm::clamp((1.05f - displacement[i]) * 2.5f, 0.0f, 1.0f);
        float speckle = 0.5f + 0.5f * rockNoise3D(unit[i] * 9.0f, params.seed ^ 0x51u, 2);
        glm::vec3 tint = glm::mix(lightStone, darkStone, glm::clamp(cavity * 0.7f + speckle * 0.4f, 0.0f, 1.0f));
        v.color = glm::vec4(tint / lightStone.x, 1.0f); // normalised so the material texture keeps its brightness
    }
    out.indices = std::move(indices);
    core::computeTangents(out.vertices, out.indices);
    return out;
}

} // namespace engine::brokenbones
