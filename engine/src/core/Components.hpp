#pragma once

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "core/Scripting.hpp"

namespace engine::core {

// The component set here is intentionally small -- exactly the two the
// implementation task asked for (Transform, Renderable) plus the two each
// subsystem needs to attach its own data to an entity (RigidBody for
// Physics, AudioSource for Audio). This is *not* the full Instance/DataModel
// property set from docs/ARCHITECTURE.md §6 -- Instance is a Luau-facing
// view over these components, built in a later pass (Scripting owns that
// translation, see Scripting.hpp's TODO).

// Every entity that exists in space has one. Physics owns writing to this
// during its step; everything else (Renderer, Audio) only reads it.
struct Transform {
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f}; // identity, (w, x, y, z)
    glm::vec3 scale{1.0f};

    [[nodiscard]] glm::mat4 matrix() const {
        glm::mat4 m = glm::translate(glm::mat4(1.0f), position);
        m *= glm::mat4_cast(rotation);
        return glm::scale(m, scale);
    }
};

// A handle into GPU-resident mesh data (core::MeshLibrary, see Mesh.hpp)
// plus the material parameters the PBR shader (shaders/scene.frag) reads
// directly -- deliberately inline here rather than behind a second
// materialHandle indirection, since there is no material *asset* system
// yet (no textures, no material library) for that indirection to be worth
// its complexity. materialHandle is kept as a placeholder for exactly that
// future indirection (texture-backed materials), unused today.
struct Renderable {
    uint32_t meshHandle = kInvalidHandle;
    uint32_t materialHandle = kInvalidHandle;
    bool visible = true;
    bool castsShadow = true;

    // Opts this entity into the GPU-instanced draw path (see
    // Renderer::drawInstancedBatches): entities sharing a meshHandle with
    // this set are batched into one vkCmdDrawIndexed(instanceCount=N) per
    // mesh instead of N individual draw calls, for repeated small props
    // (ores, rocks, foliage) where per-draw overhead dominates. Doesn't
    // affect the shadow pass, which still draws every caster individually
    // regardless of this flag -- see Renderer::drawShadowPass's comment.
    bool instanced = false;

    glm::vec4 baseColor{0.7f, 0.7f, 0.75f, 1.0f};
    float metallic = 0.05f;
    float roughness = 0.6f;
    // How strongly normalTexture perturbs the shading normal (0 = flat/
    // ignored, 1 = the sampled tangent-space normal as-authored, >1
    // exaggerated) -- scales the sampled normal's tangent-space XY before
    // renormalizing in scene.frag, the standard "normal strength" slider
    // most material editors expose. Packed into the otherwise-unused
    // ObjectPushConstants/InstanceData metallicRoughness.z at draw time
    // (see Renderer.cpp) rather than growing either struct -- see
    // SceneTypes.hpp's "z/w: unused" comment on that field.
    float normalIntensity = 1.0f;

    // Kronos ("Sky Map Full Biome Rebuild" Phase 3): real, per-object
    // opt-in for triplanar world-space texture projection (see
    // shaders/scene.frag's own triplanarWeights()/sampleTriplanar()) --
    // packed into the same otherwise-unused ObjectPushConstants/
    // InstanceData metallicRoughness.w this struct's own normalIntensity
    // (above) already established the convention for. core::Terrain sets
    // this true on every chunk it spawns/regenerates (see
    // Terrain::regenerateChunk()); every other real object in this
    // engine defaults to false and keeps sampling by inUV unchanged.
    bool useTriplanarProjection = false;

    // Adds a flat, unlit-in-the-BRDF-sense glow term on top of the normal
    // lit result -- crystals, neon signage, glowing runes. `emissiveColor`
    // is a plain RGB tint (not HDR-graded here; the post pipeline's bloom
    // pass, see PostProcess.hpp, is what makes bright emissive values
    // actually bleed/glow rather than just clip at white).
    // emissiveIntensity == 0 (the default) means "no glow", the common case.
    glm::vec3 emissiveColor{1.0f, 1.0f, 1.0f};
    float emissiveIntensity = 0.0f;

    // Kronos ("Real-Time Rendering Evolved" trailer): real glass/water
    // transmission -- see Renderer::createGlassPipeline()/shaders/glass.frag's
    // own header comment for the real Fresnel+refract() shading technique
    // this gates. `transmission == 0.0f` (the default) means "fully
    // opaque, drawn by the normal scenePipeline_ exactly as before this
    // field existed" -- a real, honest no-op for every entity that never
    // touches it. `transmissionIor` is the real index of refraction (1.5
    // for typical glass, ~1.33 for water); only meaningful once
    // `transmission > 0.0f`.
    float transmission = 0.0f;
    float transmissionIor = 1.5f;

    // Handles into a core::TextureLibrary (see Texture.hpp) -- kInvalidHandle
    // (the default) means "no texture assigned", in which case
    // Renderer::getOrCreateMaterialDescriptorSet() binds a solid-color
    // fallback (white for albedo/metallic/roughness, flat-normal for
    // normal) so scene.frag can always sample *something* without a
    // has-texture branch; the sampled value multiplies against
    // baseColor/metallicRoughness above, so an unassigned slot is a no-op,
    // not a black/broken-looking material. Only the individually-drawn
    // (non-instanced) path binds per-material textures today -- see
    // Renderer::drawInstancedBatches()'s header comment for why instanced
    // draws don't get per-instance textures in this pass (needs bindless/
    // descriptor-indexing, a real next step, not built here).
    uint32_t albedoTexture = kInvalidHandle;
    uint32_t normalTexture = kInvalidHandle;
    uint32_t metallicTexture = kInvalidHandle;
    uint32_t roughnessTexture = kInvalidHandle;
    // Ambient-occlusion map (baked cavity/crevice darkening, e.g. mortar
    // lines in stone, panel seams in brushed metal) -- multiplies into the
    // ambient term only in scene.frag/scene_rt.frag, same unassigned-is-
    // no-op fallback convention as the four texture slots above (solid
    // white == "fully unoccluded", a no-op multiply).
    uint32_t aoTexture = kInvalidHandle;

    static constexpr uint32_t kInvalidHandle = ~0u;
};

// Static never moves; Kinematic is moved directly by code (script/
// animation), pushing anything dynamic in its way, but is never itself
// pushed back (infinite mass, from the solver's perspective) -- the real
// third case Jolt's own `JPH::EMotionType` already distinguishes,
// exposed here since Physics.hpp's original RigidBody only ever
// distinguished Static/not-Static (Kinematic didn't exist as a body-
// creation option at all until this pass -- see "Physics Core" in
// README.md). A moving platform is the canonical real use: it must
// carry a rider along (a real, physical push), but nothing should be
// able to shove *it* off course.
enum class RigidBodyMotionType { Static, Kinematic, Dynamic };

[[nodiscard]] inline const char* rigidBodyMotionTypeName(RigidBodyMotionType type) {
    switch (type) {
        case RigidBodyMotionType::Static: return "Static";
        case RigidBodyMotionType::Kinematic: return "Kinematic";
        case RigidBodyMotionType::Dynamic: return "Dynamic";
    }
    return "Static";
}

// Marks an entity as owned by the physics world and caches the Jolt body ID
// so Physics::syncTransforms doesn't need a lookup table. See Physics.hpp.
struct RigidBody {
    uint32_t joltBodyId = kInvalidBodyId;
    RigidBodyMotionType motionType = RigidBodyMotionType::Static;

    static constexpr uint32_t kInvalidBodyId = ~0u;
};

// Which collider shape backs an entity's RigidBody, and its parameters --
// the physics-side counterpart to MeshSource below (Jolt body handles are
// per-session and can't be introspected back into "a box of this size,"
// the exact same problem MeshSource solves for GPU mesh handles). Real
// consumers: Studio's physics debug-draw (needs to know what wireframe to
// draw -- see "Physics Debug Tools" in README.md) and
// core::PhysicsMaterial-driven mass calculation (needs a real volume,
// which needs a real shape+size, see PhysicsMaterial.hpp's volume
// functions). Same per-kind `params` convention as MeshSource: Box:
// half-extents xyz, Sphere: radius=x (y/z unused), Capsule: radius=x/
// halfHeight=y (z unused), Mesh: unused, `path` (a real .obj) used
// instead.
enum class ColliderShapeKind { Box, Sphere, Capsule, Mesh };

struct ColliderShape {
    ColliderShapeKind kind = ColliderShapeKind::Box;
    glm::vec3 params{0.5f, 0.5f, 0.5f};
    std::string path;

    // Real, kind-specific validation: positive dimensions for Box/Sphere/
    // Capsule (a zero/negative half-extent or radius is meaningless, not
    // just unusual), and for Mesh, `path` actually resolving on disk --
    // the same real filesystem check `core::AvatarItem::validate()`
    // already established for its own meshPath.
    [[nodiscard]] bool validate(std::string& outError) const {
        switch (kind) {
            case ColliderShapeKind::Box:
                if (params.x <= 0.0f || params.y <= 0.0f || params.z <= 0.0f) {
                    outError = "box collider half-extents must all be positive";
                    return false;
                }
                return true;
            case ColliderShapeKind::Sphere:
                if (params.x <= 0.0f) {
                    outError = "sphere collider radius must be positive";
                    return false;
                }
                return true;
            case ColliderShapeKind::Capsule:
                if (params.x <= 0.0f || params.y <= 0.0f) {
                    outError = "capsule collider radius and half-height must both be positive";
                    return false;
                }
                return true;
            case ColliderShapeKind::Mesh: {
                if (path.empty()) {
                    outError = "mesh collider has no path assigned";
                    return false;
                }
                std::error_code ec;
                if (!std::filesystem::exists(path, ec) || ec) {
                    outError = "mesh collider file not found: " + path;
                    return false;
                }
                return true;
            }
        }
        outError = "unknown collider shape kind";
        return false;
    }
};

// A positioned sound emitter, mixed by Audio each frame from Transform.
// See Audio.hpp.
// Kronos ("Settings Panel v2 + Input Remapping + Accessibility Layer" --
// "Audio: Music volume, SFX volume"): real, minimal category concept --
// core::Audio::mix() multiplies a sound's own AudioSource::volume by
// this category's own real, live-settable multiplier (see
// Audio::setCategoryVolume()'s own comment) on top of the real, separate
// master volume (miniaudio's own engine-level volume). Two categories
// only, matching exactly what the settings spec names -- not a general
// N-bus mixer.
enum class AudioCategory { Music, SFX };

struct AudioSource {
    uint32_t soundHandle = kInvalidHandle;
    float volume = 1.0f;
    float minDistance = 1.0f;
    float maxDistance = 50.0f;
    bool looping = false;
    bool playing = false;
    AudioCategory category = AudioCategory::SFX;

    static constexpr uint32_t kInvalidHandle = ~0u;
};

// Debug/editor-facing name -- Explorer (Studio §5) reads this, gameplay code
// generally shouldn't depend on it.
struct Name {
    std::string value;
};

// Which procedural generator (Mesh::createBox/createPlane/createCapsule/
// createQuad) built an entity's Renderable::meshHandle, or Obj if it came
// from core::loadObj() -- the same "how to regenerate this mesh" idea as
// core::PrefabMeshKind (see Prefab.hpp's comment), duplicated here as its
// own small enum rather than reused directly: PrefabMeshKind describes a
// *reusable template*'s shape and has no "imported file" case to describe
// (a Prefab is always procedural today), while this describes what a
// *live scene entity's* mesh actually is right now, which does need that
// 5th case. Coupling this to Prefab's format would also be the wrong
// dependency direction -- scene serialization should be free to evolve
// independently of the prefab-template format.
enum class MeshSourceKind { Box, Plane, Capsule, Quad, Obj };

// Attached alongside Renderable on any entity whose mesh a scene save
// needs to be able to rebuild later -- core::MeshLibrary handles are
// per-session, registration-order indices (see Mesh.hpp's MeshLibrary
// comment), never stable across a save/load round trip, so
// Renderable::meshHandle alone is not enough to reconstruct anything.
// `params` follows Prefab::meshParams' per-kind convention (Box:
// half-extents xyz, Plane: halfWidth=x/halfDepth=z, Capsule: radius=x/
// halfHeight=y, Quad: halfSize=x) and is unused when kind == Obj, where
// `path` (a real filesystem path to the source .obj) is used instead.
// An entity with Renderable but no MeshSource is legal (its mesh simply
// won't survive a save/load round trip) -- see studio/SceneManager.hpp
// for where that gap is surfaced rather than silently producing a
// meshless entity.
struct MeshSource {
    MeshSourceKind kind = MeshSourceKind::Box;
    glm::vec3 params{0.5f, 0.5f, 0.5f};
    std::string path;
};

// Marks an entity as GPU-skinned -- rendered through
// core::Renderer::drawSkinnedEntities() (a real, separate draw path from
// Renderable's, see Renderer.hpp's AuxiliarySceneHandle/skinning
// comments) instead of the plain opaque pass. `riggedMeshHandle` indexes
// core::RiggedMeshLibrary (mirroring how Renderable::meshHandle indexes
// core::MeshLibrary). `skinningMatrices` is this entity's *current pose*
// -- one matrix per joint, each already composed as
// currentJointWorldMatrix * skeleton.inverseBindMatrices()[joint] (see
// core::AnimationPlayer::computeSkinningMatrices()) -- written fresh
// every tick by whatever drives this entity's animation
// (core::AnimationPlayer for a real playing clip, or a caller that just
// wants a static bind pose can fill it with identity matrices). Material
// fields are the same shape as Renderable's own, deliberately not
// shared via inheritance/composition -- SkinnedRenderable and Renderable
// are mutually exclusive on one entity (an entity is either a plain
// static mesh or a skinned one, never both), so there's no real code
// asking for "a Renderable that happens to also skin" today.
struct SkinnedRenderable {
    static constexpr uint32_t kInvalidHandle = ~0u;

    uint32_t riggedMeshHandle = kInvalidHandle;
    std::vector<glm::mat4> skinningMatrices;

    bool visible = true;
    // NOT wired to anything yet -- drawShadowPass() only iterates
    // Renderable-having entities (see Renderer.cpp), so skinned entities
    // never cast shadows today regardless of this flag. A real skinned
    // shadow pass needs its own shadow_skinned.vert (the shadow pass's
    // vertex shader would need the same joint-blend logic
    // scene_skinned.vert has), a real, separate, not-yet-built follow-up
    // -- kept here so the field exists and defaults sanely once that
    // exists, rather than adding it as a breaking change later.
    bool castsShadow = true;

    glm::vec4 baseColor{0.8f, 0.8f, 0.8f, 1.0f};
    float metallic = 0.05f;
    float roughness = 0.6f;
    glm::vec3 emissiveColor{1.0f, 1.0f, 1.0f};
    float emissiveIntensity = 0.0f;
};

// A real, minimal moving platform -- oscillates along `axis` around
// `basePosition` with a sine wave, the "jumpable platform" runtime
// example scene (docs task category 7) actually needs to demonstrate
// something worth jumping onto/off of, not just a static block. Driven
// every tick by a caller (Application.cpp's pre-tick hook) via
// Physics::moveKinematic() -- this entity's RigidBody must be Kinematic
// (Physics::createKinematicBox()), never Dynamic/Static, or
// moveKinematic() has nothing to move.
struct MovingPlatform {
    glm::vec3 basePosition{0.0f};
    glm::vec3 axis{0.0f, 1.0f, 0.0f}; // normalized direction of travel
    float amplitude = 2.0f;           // meters from basePosition at the extremes
    float period = 4.0f;              // seconds for one full back-and-forth cycle
};

// Pure -- given how long the world has been simulating, where should
// this platform be right now. A plain sine wave (not e.g. a clamped
// ping-pong) so the motion is smooth and continuous with no
// direction-reversal snap.
[[nodiscard]] inline glm::vec3 movingPlatformTarget(const MovingPlatform& platform, float totalSimTime) {
    constexpr float kTwoPi = 6.28318530718f;
    float phase = platform.period > 0.0f ? (kTwoPi * totalSimTime / platform.period) : 0.0f;
    return platform.basePosition + platform.axis * (platform.amplitude * std::sin(phase));
}

// Kronos (Alpha Roadmap Phase 2, "Scene system"): real parent-child
// relationship -- confirmed absent from this codebase entirely before
// this (no Parent/Children concept anywhere), the real blocker for both
// a genuine hierarchy panel (previously a flat category list, see
// studio/panels/ExplorerPanel.cpp) and any scene-graph transform
// propagation. Only ever attached via core::hierarchy::setParent()
// (core/Hierarchy.hpp) -- never construct/mutate this directly, since
// that function is what keeps a parent's `children` list and a child's
// own `parent` field consistent with each other (a directly-added
// Hierarchy with a mismatched parent/children pair is real, silent data
// corruption the rest of core::hierarchy's functions all assume can't
// happen).
//
// A real, deliberate semantic: once an entity has a Hierarchy with
// parent != kNullEntity, its own Transform is interpreted as *local*
// space (relative to the parent), not world space -- see
// core::hierarchy::computeWorldMatrix()'s own comment. An entity with no
// Hierarchy component at all (the overwhelming majority, and every
// entity that existed before this feature) keeps Transform meaning
// exactly what it always has: world space. This is why every existing
// system (Physics, Audio, Scripting's world.getPosition()) is left
// reading Transform directly, unchanged -- only entities a caller
// explicitly parents opt into the new local-space meaning, so this is a
// strictly additive change with zero behavior change for anything that
// existed before it.
struct Hierarchy {
    entt::entity parent = entt::null; // core::kNullEntity -- see ECS.hpp
    std::vector<entt::entity> children;
};

// Kronos (Alpha Roadmap Phase 3, "Component system"): a real, entity-
// driven point light -- see Renderer.cpp's own point-light UBO-fill
// comment for where these get combined with (and capped alongside)
// SceneLighting::pointLights' own manually-authored entries against the
// same kMaxPointLights budget (SceneTypes.hpp). Position is read from the
// entity's own Transform (via core::hierarchy::computeWorldMatrix(), so a
// Light parented under a moving rig tracks its real world position, not
// just its local offset) -- deliberately not duplicated onto this
// component, same "position lives on Transform" convention every other
// component here already follows.
struct Light {
    bool enabled = true;
    glm::vec3 color{1.0f};
    float intensity = 1.0f;
    float radius = 10.0f;
};

// Kronos (Alpha Roadmap Phase 3, "Component system"): a real, entity-
// attached gameplay script -- wired into the existing sandboxed
// core::Scripting VM (see core/Application.cpp's pre-tick hook, right
// alongside the other general per-tick systems like particleSystem_/
// animationPlayer_). `source` is inline Luau source, not a file path --
// no asset-provenance tracking for script files exists yet (the same
// real gap SceneFile.hpp's own comment already flags for texture files),
// so inline source is the honest, real thing to store today, matching
// what Studio's own script authoring already produces
// (studio/panels/ScriptEditorPanel.hpp).
//
// A script that needs to act on its own entity does so the same way
// every other Luau binding in this engine already requires --
// world.findByName() against a stable Name component (see
// ScriptWorldApi.hpp's own header comment on why entities are plain
// numbers, not a Roblox-style `script.Parent`/self-reference: there's no
// Instance/DataModel layer for that to hang off of yet).
struct Script {
    std::string source;
    bool autoRun = true;                // real-loaded automatically the first tick this entity's Script is seen
    ScriptId scriptId = kInvalidScript;  // kInvalidScript until autoRun has actually loaded it

    // Kronos (Alpha Roadmap Phase 7, "Lua Scripting Platform" -- "Lua
    // hot-reload"): real bookkeeping, not authored data -- the exact
    // `source` that was compiled into `scriptId`. Application's own
    // per-tick scan (Application.cpp) compares this against `source`
    // every tick; a mismatch (source changed since the last load, e.g. a
    // future script editor writing directly into this component) means
    // real-unload the stale scriptId and real-reload the new source, the
    // same "detect a change, tear down, rebuild fresh" shape
    // studio::plugins::ScriptedPlugin's own scriptChangedOnDisk()+
    // reload() already established for Studio's file-backed plugin
    // scripts -- this is that same real capability for inline-source
    // gameplay scripts, which have no file to watch.
    std::string loadedSource;
};

} // namespace engine::core
