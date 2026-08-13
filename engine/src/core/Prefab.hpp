#pragma once

#include <string>

#include <glm/glm.hpp>

namespace engine::core {

// Which procedural generator (Mesh::createBox/createPlane/createCapsule/
// createQuad) a prefab's shape resolves to, plus meshParams' 3 floats
// (interpretation depends on the kind -- see studio/plugins/PrefabPlugin.cpp's
// buildMesh()). A prefab stores "how to regenerate this mesh", not a raw
// GPU handle: mesh handles are per-session registration-order indices
// (see MeshLibrary.hpp), not stable across a save/load round trip, the
// same problem core::AnimationTrack solved by targeting entities by Name
// instead of EntityId -- this is that same fix applied to meshes instead.
// Real asset-file meshes (once migration::AssetConverter's mesh path
// stops being a stub) are a natural 5th PrefabMeshKind, not a redesign.
enum class PrefabMeshKind { Box, Plane, Capsule, Quad };

[[nodiscard]] const char* prefabMeshKindName(PrefabMeshKind kind);

// A reusable object template -- shape (kind + generator params) and
// material, saved/loaded as a small text file, spawned as a real ECS
// entity (Transform + Renderable) as many times as wanted. Pure data:
// building the actual GPU mesh and creating entities from this needs
// Vulkan handles and an ECS/MeshLibrary this struct deliberately doesn't
// own, same reasoning as core::Animation/ParticleSystem -- see
// studio/plugins/PrefabPlugin for where that happens.
struct Prefab {
    std::string name = "New Prefab";

    PrefabMeshKind meshKind = PrefabMeshKind::Box;
    glm::vec3 meshParams{0.5f, 0.5f, 0.5f}; // Box: half-extents xyz. Plane: halfWidth=x, halfDepth=z. Capsule: radius=x, halfHeight=y. Quad: halfSize=x.

    glm::vec3 scale{1.0f};
    glm::vec4 baseColor{0.7f, 0.7f, 0.75f, 1.0f};
    float metallic = 0.05f;
    float roughness = 0.6f;
    glm::vec3 emissiveColor{1.0f, 1.0f, 1.0f};
    float emissiveIntensity = 0.0f;
    bool castsShadow = true;
    bool instanced = false;

    [[nodiscard]] bool saveToFile(const std::string& path) const;
    [[nodiscard]] bool loadFromFile(const std::string& path);
};

} // namespace engine::core
