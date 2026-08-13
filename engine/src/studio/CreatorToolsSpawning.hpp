#pragma once

#include <string>

#include <glm/glm.hpp>

#include "core/ECS.hpp"
#include "core/Navigation.hpp"
#include "core/WorldProp.hpp"

namespace engine::studio {

// Sprint 7 ("Studio UI Revamp"): the real, ECS-only half of
// CreatorToolsPlugin's authoring actions -- split out from the plugin
// itself (which also owns Vulkan mesh-registration state -- see
// plugins/CreatorToolsPlugin.hpp's class comment) so entity-creation
// logic gets real, headless test coverage the same way
// core::spawnWorldProp() already does, rather than being locked behind a
// live VmaAllocator/VkDevice nobody but a real Studio process has.
// `boxMeshHandle`/`capsuleMeshHandle` are caller-registered
// core::MeshLibrary handles -- this module never touches Vulkan itself.

// Creates a real authoring-only prop entity: Transform + Renderable +
// MeshSource + WorldProp marker (+ Interactable/LampState for the three
// interactive kinds, see WorldProp.hpp) -- deliberately no physics body
// (Studio never runs one outside PhysicsPreviewPlugin's own sandboxed
// Play mode). `spawnIndex` becomes part of the entity's display name
// ("Tree 3") so repeated spawns don't collide.
core::EntityId spawnPropAuthoring(core::ECS& ecs, core::WorldPropKind kind, glm::vec3 position, int spawnIndex,
                                   uint32_t boxMeshHandle, uint32_t capsuleMeshHandle);

// Creates a real authoring-only teleport pad entity: Transform +
// Renderable + Interactable + TeleportPad + NavMarker. `spawnIndex`
// names it ("TeleportPad 2") the same way spawnPropAuthoring does.
core::EntityId spawnTeleportPadAuthoring(core::ECS& ecs, glm::vec3 position, glm::vec3 destination,
                                          const std::string& linkTag, int spawnIndex, uint32_t boxMeshHandle);

// Sprint 9 ("Creator Tools Phase 1") task category 4's "tool for placing
// ... nav markers". Creates a real, invisible location tag -- Transform +
// NavMarker only, deliberately no Renderable -- the exact same shape
// main.cpp's own SpawnPoint/Shop/UpgradeKiosk markers already use (see
// that file's marker-spawning blocks): a nav marker is a location tag for
// a future minimap/quest UI to query (core::findNavMarkers()), not a
// physical object a player would ever see or collide with.
core::EntityId spawnNavMarkerAuthoring(core::ECS& ecs, glm::vec3 position, core::NavMarkerKind kind,
                                        const std::string& label, int spawnIndex);

// Kronos (Alpha Roadmap Phase 6, "Tooling Layer" -- "Lighting editor"):
// creates a real, entity-driven point light -- Transform + Light
// (Components.hpp, Alpha Roadmap Phase 3) + a small emissive Renderable/
// MeshSource so it's actually visible/selectable in the Viewport (a
// Light component alone has no mesh, and an invisible-in-editor light
// would be real but unusable to place by eye) -- no separate marker
// component needed the way NavMarker/WorldProp are, since Light itself
// is already the real, queryable "this is a light" fact. `spawnIndex`
// names it ("PointLight 2") the same way every other spawnXAuthoring()
// here does.
core::EntityId spawnPointLightAuthoring(core::ECS& ecs, glm::vec3 position, int spawnIndex, uint32_t boxMeshHandle);

} // namespace engine::studio
