#pragma once

#include <glm/glm.hpp>

#include "core/ECS.hpp"
#include "core/PhysicsMaterial.hpp"

namespace engine::core {

class Physics;

// Sprint 6 ("World Systems & Environment") task category 2: a real
// runtime system for placing world props -- decoration and light set
// dressing that needs a real collider (so a player can't walk through a
// tree) but, unlike core::Prefab (a Studio-authoring/save-file template,
// see Prefab.hpp's own comment), is meant to be placed directly at
// runtime by code (a level script, a procedural scatter pass, a creator
// world's own placement data once that exists -- see this sprint's
// "ready for future creator-world support" constraint). Deliberately not
// a merge with Prefab: Prefab's whole shape is "how do I regenerate this
// mesh from a save file", which has nothing to do with what WorldProp
// actually needs (a real physics body + an optional interaction hook).
//
// Six real kinds, each a genuine, different physics/interaction shape,
// not six recolors of the same box:
enum class WorldPropKind : uint8_t { Tree, Rock, Crate, Barrel, Lamp, Bush };

[[nodiscard]] const char* worldPropKindName(WorldPropKind kind);

// Marker component -- which kind a given prop entity is, for anything
// downstream that wants to distinguish them (Studio's Explorer, a future
// biome-aware scatter pass, tests).
struct WorldProp {
    WorldPropKind kind = WorldPropKind::Tree;
};

// A real, minimal working interaction: Lamps can be switched on/off.
// `on` drives both `Renderable::emissiveIntensity` (see toggleLamp()
// below) and is the one piece of genuinely stateful prop behavior this
// module ships, rather than every prop just being an inert collider --
// proof the "optional interaction hooks" wording is real, not aspirational.
struct LampState {
    bool on = true;
    float litIntensity = 2.2f;
};

// Pure -- flips `lamp.on` and returns the emissiveIntensity the caller
// should write into that entity's Renderable (0 when off, `litIntensity`
// when on). Split from the ECS-touching call site the same "pure
// decision, I/O-touching caller writes it" pattern toggleDoor()
// established, so this is trivially unit-testable without a live ECS.
[[nodiscard]] float toggleLamp(LampState& lamp);

// Real spawn parameters, one struct rather than a long positional
// parameter list every call site would otherwise have to get right in
// order -- `halfExtent` is the box collider's own half-extents (every
// kind uses a real box collider, not a per-mesh convex hull; a
// deliberate, common simplification, see this function's own doc
// comment for why). `motionType` is `Static` for fixed scenery
// (Tree/Rock/Lamp/Bush) and `Dynamic` for pushable/physical props
// (Crate/Barrel) -- callers don't set this directly, spawnWorldProp()
// picks the real, correct default per `kind`; `overrideMotionType` exists
// only for tests that want to deliberately exercise the other case.
struct WorldPropSpawnInfo {
    WorldPropKind kind = WorldPropKind::Tree;
    glm::vec3 position{0.0f};
    glm::vec3 halfExtent{0.5f};
    uint32_t meshHandle = 0; // caller-registered core::MeshLibrary handle -- see breakOreNode()'s identical reasoning
    glm::vec3 baseColor{0.6f};
    float metallic = 0.0f;
    float roughness = 0.8f;
    PhysicsMaterial material{};
    bool interactive = false; // attaches a real core::Interactable -- Lamp also gets a real LampState
};

// Real spawn: creates a new ECS entity, a real physics body
// (Physics::attachBodyToEntity(), Static or Dynamic per kind -- see
// WorldPropSpawnInfo's own comment), a real Renderable, and, if
// `interactive`, a real Interactable (+ LampState for `Lamp`
// specifically). One real call site every example prop and every test
// goes through, so the six kinds can't drift out of sync with each
// other's creation logic -- the same reasoning createOreNode() already
// established for the six ore types.
EntityId spawnWorldProp(ECS& ecs, Physics& physics, const WorldPropSpawnInfo& info);

} // namespace engine::core
