#include "brokenbones/CliffMap.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

#include <glm/gtc/matrix_transform.hpp>

#include "brokenbones/RockMesh.hpp"
#include "core/Components.hpp"

namespace engine::brokenbones {

namespace {

void applyMaterial(core::Renderable& renderable, const core::PbrTextureSet& set, float roughness,
                   glm::vec3 tint = glm::vec3(1.0f)) {
    renderable.baseColor = glm::vec4(tint, 1.0f);
    renderable.metallic = 0.0f;
    renderable.roughness = roughness;
    renderable.albedoTexture = set.albedo;
    renderable.normalTexture = set.normal;
    renderable.metallicTexture = set.metallic;
    renderable.roughnessTexture = set.roughness;
    renderable.aoTexture = set.ao;
}

// Flat grid whose normals follow a few crossing swells, so reflections ripple instead of mirroring.
void buildWaterMesh(glm::vec2 halfSize, uint32_t seed, std::vector<core::Vertex>& outVertices,
                    std::vector<uint32_t>& outIndices) {
    outVertices.clear();
    outIndices.clear();
    struct Swell {
        glm::vec2 k;
        float slope, phase;
    };
    std::array<Swell, 4> swells{};
    for (size_t i = 0; i < swells.size(); ++i) {
        float angle = static_cast<float>((seed >> (i * 5)) % 360) * 0.01745f + static_cast<float>(i) * 1.3f;
        float wavelength = 2.5f + 2.2f * static_cast<float>(i);
        float k = 6.2832f / wavelength;
        swells[i] = {glm::vec2(std::cos(angle), std::sin(angle)) * k, 0.07f / (1.0f + 0.4f * static_cast<float>(i)),
                     static_cast<float>(i) * 2.1f};
    }
    constexpr float kSpacing = 0.6f;
    int nx = std::max(2, static_cast<int>(2.0f * halfSize.x / kSpacing));
    int nz = std::max(2, static_cast<int>(2.0f * halfSize.y / kSpacing));
    for (int iz = 0; iz <= nz; ++iz) {
        for (int ix = 0; ix <= nx; ++ix) {
            glm::vec2 p(-halfSize.x + 2.0f * halfSize.x * static_cast<float>(ix) / static_cast<float>(nx),
                        -halfSize.y + 2.0f * halfSize.y * static_cast<float>(iz) / static_cast<float>(nz));
            glm::vec2 grad(0.0f);
            for (const Swell& s : swells) grad += s.k / glm::length(s.k) * s.slope * std::cos(glm::dot(s.k, p) + s.phase);
            core::Vertex vertex;
            vertex.position = glm::vec3(p.x, 0.0f, p.y);
            vertex.normal = glm::normalize(glm::vec3(-grad.x, 1.0f, -grad.y));
            vertex.uv = p * 0.1f;
            outVertices.push_back(vertex);
        }
    }
    for (int iz = 0; iz < nz; ++iz) {
        for (int ix = 0; ix < nx; ++ix) {
            uint32_t a = static_cast<uint32_t>(iz * (nx + 1) + ix);
            uint32_t b = a + 1;
            uint32_t c = a + static_cast<uint32_t>(nx + 1);
            uint32_t d = c + 1;
            outIndices.insert(outIndices.end(), {a, c, b, b, c, d});
        }
    }
    core::computeTangents(outVertices, outIndices);
}

} // namespace

void buildBoxMesh(glm::vec3 h, float uvPerMetre, std::vector<core::Vertex>& outVertices,
                  std::vector<uint32_t>& outIndices) {
    outVertices.clear();
    outIndices.clear();
    struct Face {
        glm::vec3 normal, u, v;
        float uSize, vSize;
    };
    const Face faces[6] = {
        {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}, h.z, h.y},  {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}, h.z, h.y},
        {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}, h.x, h.z},  {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}, h.x, h.z},
        {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}, h.x, h.y},   {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}, h.x, h.y},
    };
    for (const Face& f : faces) {
        glm::vec3 center = f.normal * glm::abs(glm::dot(f.normal, h));
        uint32_t base = static_cast<uint32_t>(outVertices.size());
        const glm::vec2 corners[4] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
        for (const glm::vec2& c : corners) {
            core::Vertex vertex;
            vertex.position = center + f.u * (c.x * f.uSize) + f.v * (c.y * f.vSize);
            vertex.normal = f.normal;
            vertex.uv = glm::vec2(c.x * f.uSize, c.y * f.vSize) * uvPerMetre;
            outVertices.push_back(vertex);
        }
        outIndices.insert(outIndices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }
    core::computeTangents(outVertices, outIndices);
}

uint32_t CliffMap::uploadMesh(const GpuUpload& gpu, const std::vector<core::Vertex>& vertices,
                              const std::vector<uint32_t>& indices) {
    core::Mesh mesh;
    if (!mesh.uploadFromHost(gpu.allocator, gpu.device, gpu.commandPool, gpu.queue, vertices, indices)) {
        return core::Renderable::kInvalidHandle;
    }
    if (nextMeshSlot_ < meshPool_.size()) {
        uint32_t handle = meshPool_[nextMeshSlot_++];
        gpu.meshLibrary->replaceMesh(handle, std::move(mesh), gpu.allocator);
        return handle;
    }
    uint32_t handle = gpu.meshLibrary->registerMesh(std::move(mesh));
    meshPool_.push_back(handle);
    ++nextMeshSlot_;
    return handle;
}

bool CliffMap::build(const CliffLayout& layout, core::ECS& ecs, core::Physics& physics, const GpuUpload& gpu,
                     const core::ProceduralMaterialLibrary& materials) {
    clear(ecs, physics);
    nextMeshSlot_ = 0;

    core::PhysicsMaterial rockPhysics;
    rockPhysics.friction = 0.8f;
    rockPhysics.restitution = 0.1f;

    std::vector<glm::vec3> collisionPositions;
    auto buildStaticMesh = [&](const char* name, const std::vector<core::Vertex>& vertices,
                               const std::vector<uint32_t>& indices, const core::PbrTextureSet& material,
                               float roughness, glm::vec3 tint) {
        collisionPositions.resize(vertices.size());
        for (size_t i = 0; i < vertices.size(); ++i) collisionPositions[i] = vertices[i].position;
        core::EntityId entity = physics.createMeshBody(ecs, glm::vec3(0.0f), collisionPositions, indices, rockPhysics);
        if (entity == core::kNullEntity) return false;
        entities_.push_back(entity);
        if (auto* nameComponent = ecs.tryGetComponent<core::Name>(entity)) nameComponent->value = name;
        auto* renderable = ecs.tryGetComponent<core::Renderable>(entity);
        if (renderable == nullptr) return false;
        renderable->meshHandle = uploadMesh(gpu, vertices, indices);
        applyMaterial(*renderable, material, roughness, tint);
        return renderable->meshHandle != core::Renderable::kInvalidHandle;
    };

    std::vector<core::Vertex> boxVertices;
    std::vector<uint32_t> boxIndices;
    auto buildStaticBox = [&](const char* name, glm::vec3 center, glm::vec3 halfExtents, glm::quat rotation,
                              const core::PbrTextureSet& material, float roughness, float uvPerMetre,
                              glm::vec3 tint = glm::vec3(1.0f)) {
        core::EntityId entity =
            physics.createStaticBox(ecs, center, halfExtents, rotation, rockPhysics, core::CollisionLayer::Static);
        entities_.push_back(entity);
        if (auto* nameComponent = ecs.tryGetComponent<core::Name>(entity)) nameComponent->value = name;
        if (auto* transform = ecs.tryGetComponent<core::Transform>(entity)) {
            transform->position = center;
            transform->rotation = rotation;
        }
        buildBoxMesh(halfExtents, uvPerMetre, boxVertices, boxIndices);
        if (auto* renderable = ecs.tryGetComponent<core::Renderable>(entity)) {
            renderable->meshHandle = uploadMesh(gpu, boxVertices, boxIndices);
            applyMaterial(*renderable, material, roughness, tint);
        }
    };
    const MapThemeInfo& theme = mapThemeInfo(layout.theme);

    if (!buildStaticMesh("CliffFace", layout.faceVertices, layout.faceIndices, materials.stone, 0.85f, layout.rockTint)) {
        std::fprintf(stderr, "brokenbones: failed to build the cliff face.\n");
        return false;
    }

    const glm::quat identity(1.0f, 0.0f, 0.0f, 0.0f);
    buildStaticBox("Plateau", layout.plateau.center, layout.plateau.halfExtents, identity, materials.ground, 0.9f, 0.15f,
                   theme.groundTint);
    buildStaticBox("DivingBoard", layout.divingBoard.center, layout.divingBoard.halfExtents, identity, materials.wood,
                   0.7f, 0.6f);
    for (const CliffBeam& beam : layout.beams) {
        buildStaticBox("Beam", beam.position, beam.halfExtents, beam.rotation, materials.wood, 0.75f, 0.6f);
    }
    for (size_t i = 0; i < layout.groundPieces.size(); ++i) {
        const AxisBox& piece = layout.groundPieces[i];
        bool lagoonFloor = i + 1 == layout.groundPieces.size();
        buildStaticBox(lagoonFloor ? "LagoonFloor" : "Ground", piece.center, piece.halfExtents, identity,
                       lagoonFloor ? materials.sand : materials.ground, 0.95f, 0.12f, theme.groundTint);
    }

    for (const CliffBoulder& boulder : layout.boulders) {
        RockParams params;
        params.seed = boulder.seed;
        params.radius = boulder.radius;
        params.stretch = boulder.stretch;
        RockMeshData rock = generateRockMesh(params);
        glm::mat4 placement = glm::translate(glm::mat4(1.0f), boulder.position) * glm::mat4_cast(boulder.rotation);
        glm::mat3 normalRotation = glm::mat3_cast(boulder.rotation);
        for (core::Vertex& v : rock.vertices) {
            v.position = glm::vec3(placement * glm::vec4(v.position, 1.0f));
            v.normal = normalRotation * v.normal;
        }
        buildStaticMesh("Boulder", rock.vertices, rock.indices, materials.stone, 0.8f, layout.rockTint);
    }

    // Visual only: the run ends on contact, so the water needs no body.
    {
        glm::vec2 halfSize = 0.5f * (layout.lagoonMax - layout.lagoonMin);
        glm::vec2 center = 0.5f * (layout.lagoonMax + layout.lagoonMin);
        std::vector<core::Vertex> waterVertices;
        std::vector<uint32_t> waterIndices;
        buildWaterMesh(halfSize, layout.seed, waterVertices, waterIndices);
        core::EntityId water = ecs.createEntity("Lagoon");
        entities_.push_back(water);
        if (auto* transform = ecs.tryGetComponent<core::Transform>(water)) {
            transform->position = glm::vec3(center.x, layout.waterY, center.y);
        }
        auto& renderable = ecs.addComponent<core::Renderable>(water);
        renderable.meshHandle = uploadMesh(gpu, waterVertices, waterIndices);
        renderable.baseColor = glm::vec4(theme.waterColor, 1.0f);
        renderable.metallic = 0.0f;
        bool glowing = glm::length(theme.waterGlow) > 0.0f;
        renderable.roughness = glowing ? 0.55f : 0.07f;
        renderable.emissiveColor = theme.waterGlow;
        renderable.emissiveIntensity = glowing ? 4.0f : 0.0f;
        renderable.castsShadow = false;
        renderable.layers.waterWaves = glowing ? 0.35f : 1.0f;
        renderable.layers.waterFoam = glowing ? 0.0f : 0.7f;
    }

    physics.optimizeBroadPhase();
    return true;
}

void CliffMap::clear(core::ECS& ecs, core::Physics& physics) {
    for (core::EntityId entity : entities_) {
        physics.detachBody(entity, ecs);
        ecs.destroyEntity(entity);
    }
    entities_.clear();
}

} // namespace engine::brokenbones
