#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "core/Mesh.hpp"

namespace engine::brokenbones {

struct RockMeshData {
    std::vector<core::Vertex> vertices;
    std::vector<uint32_t> indices;
};

struct RockParams {
    uint32_t seed = 1;
    float radius = 1.0f;
    glm::vec3 stretch{1.0f}; // per-axis scale applied after displacement
    float roughness = 0.3f;  // noise displacement as a fraction of radius
    int facetCuts = 6;       // planar cuts that give the chiselled look
    int subdivisions = 3;
};

// Deterministic for a given RockParams; centred on the origin.
[[nodiscard]] RockMeshData generateRockMesh(const RockParams& params);

// Smooth 3D fractal noise in roughly [-1, 1].
[[nodiscard]] float rockNoise3D(glm::vec3 p, uint32_t seed, int octaves = 4);

} // namespace engine::brokenbones
