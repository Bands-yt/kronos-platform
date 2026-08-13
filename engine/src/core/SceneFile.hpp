#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "core/Components.hpp"
#include "core/ParticleSystem.hpp"

namespace engine::core {

// One entity's worth of persisted scene data -- a pure DTO, not an ECS
// component itself (same separation as core::Prefab: building the real
// GPU mesh and creating the real entity needs Vulkan handles and an ECS
// this struct deliberately doesn't own -- see studio/SceneManager.hpp for
// where that happens). Only entities with both Transform and a non-empty
// Name are round-trippable at all (an unnamed entity has nothing for a
// future load to re-identify it by, same reasoning as
// core::AnimationTrack targeting by name instead of EntityId -- see
// Prefab.hpp's comment on the same problem for mesh handles).
struct SceneEntityRecord {
    std::string name;

    // Kronos Alpha Roadmap Phase 2 ("Scene system"): the entity's real
    // parent, identified by name -- same "round-trip by name, not
    // EntityId" convention this whole file already uses (EntityId is a
    // per-session handle; name is the only thing a future load can
    // re-identify an entity by). Empty means the entity is a real root
    // (core::Hierarchy::parent == kNullEntity, or no Hierarchy component
    // at all) -- see studio/SceneManager.cpp for the two-pass rebuild
    // this requires (every entity must exist before any parentName can
    // be resolved to a live EntityId).
    std::string parentName;

    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};

    // Renderable + MeshSource travel together: a Renderable with no
    // MeshSource attached has no way to regenerate its GPU mesh on load
    // (see MeshSource's comment), so hasRenderable implies the fields
    // below are meaningful, but meshSource may still be "empty" (an
    // entity that had Renderable but no MeshSource at save time) --
    // studio::SceneManager surfaces that gap rather than silently
    // producing a meshless entity, see its class comment.
    bool hasRenderable = false;
    bool hasMeshSource = false;
    MeshSource meshSource;
    glm::vec4 baseColor{0.7f, 0.7f, 0.75f, 1.0f};
    float metallic = 0.05f;
    float roughness = 0.6f;
    float normalIntensity = 1.0f;
    glm::vec3 emissiveColor{1.0f, 1.0f, 1.0f};
    float emissiveIntensity = 0.0f;
    bool castsShadow = true;
    bool instanced = false;

    bool hasParticleEmitter = false;
    ParticleEmitterSettings emitter;

    // Kronos (Alpha Roadmap Phase 3, "Component system"): a real Light --
    // see Components.hpp's own Light comment. Position isn't duplicated
    // here, same as it isn't on the live component -- it's real Transform
    // above.
    bool hasLight = false;
    Light light;
};

// A full scene -- every SceneEntityRecord worth persisting, plus the
// editor camera's pose, saved/loaded as a real text file using the same
// "KEY value per line, repeatable sub-block" convention
// core::AnimationClip already established (ENTITY starts a new record
// the same way AnimationClip's TRACK does; unrecognized lines are
// skipped, not errors, for forward-compatibility).
//
// Deliberately NOT covered yet (real, stated gaps, not silently
// dropped): core::Terrain (no heightmap accessor/serialization exists
// anywhere in the engine yet -- a real, separate feature), and material
// textures on Renderable (albedoTexture/normalTexture/etc -- like mesh
// handles, texture handles are per-session and nothing anywhere records
// the source file path a texture was loaded from, so there is nothing
// to serialize from yet; building that provenance tracking is the
// texture-pipeline equivalent of MeshSource and deserves its own pass).
// core::RigidBody/AudioSource aren't included either: Studio creates no
// entities with either component today (no Physics in Studio at all --
// see StudioApp.hpp's class comment -- and no audio-source authoring UI
// exists), so there is nothing yet to round-trip.
//
// Pure data: this struct doesn't know about MeshLibrary, Vulkan, or
// core::ECS at all -- see studio/SceneManager.hpp for the capture-from-
// live-ECS / rebuild-into-live-ECS half.
struct SceneFile {
    std::vector<SceneEntityRecord> entities;

    glm::vec3 cameraPosition{0.0f, 2.0f, 6.0f};
    float cameraYawDegrees = -90.0f;
    float cameraPitchDegrees = -10.0f;
    float cameraFovDegrees = 60.0f;

    [[nodiscard]] bool saveToFile(const std::string& path) const;
    [[nodiscard]] bool loadFromFile(const std::string& path);
};

} // namespace engine::core
