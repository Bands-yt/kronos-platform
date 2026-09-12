#pragma once

#include <glm/glm.hpp>
#include <volk.h>
#include <vk_mem_alloc.h>

#include "core/ECS.hpp"
#include "core/Mesh.hpp"
#include "core/Physics.hpp"

namespace engine::despair {

// PROJECT: DESPAIR -- the ECS/Physics/Vulkan-owning half of
// computeFacilityLayout() (FacilityLayout.hpp), mirroring
// housedemo::buildHouseDemoScene()'s own split. Every FacilityWall gets
// both a real Physics::createStaticBox collider and a Renderable visual
// -- unlike HouseDemoScene's purely decorative walls, a facility's
// walls/floor have to actually block movement and occlude the
// core::Physics::raycast() line-of-sight checks SanitySystem's gaze
// detection and HorrorAIManager's hasLineOfSight both depend on. On top
// of that generic geometry list, this spawns every hand-placed special
// entity the layout calls for: LootContainer, Keycard+core::Pickup,
// the corridor's LockedDoor+core::Door+core::Interactable (visual-only,
// no collider -- see FacilityDoorSpec's own comment on why that's a
// deliberate, precedent-following choice, not an oversight), HidingSpot,
// and all three HorrorAIManager AI tiers (spawned Dormant/Idle with
// struct defaults -- HorrorAIManager's own tick*() functions require
// these entities to already exist in its ecs.view<>() scans; nothing
// spawns them lazily).
//
// Does not touch core::Terrain (unlike HouseDemoScene) -- this is an
// indoor facility with its own floor slabs, not an outdoor lot needing a
// heightmap underneath it. Does not attach despair::FPSPlayerSettings or
// spawn the player character -- that's the caller's own job (see
// main.cpp's --despair mode), the same way HouseDemoScene never spawns a
// character either.
void buildFacilityScene(core::ECS& ecs, core::Physics& physics, core::MeshLibrary& meshLibrary, VmaAllocator allocator,
                         VkDevice device, VkCommandPool cmdPool, VkQueue queue, glm::vec3 worldOrigin);

} // namespace engine::despair
