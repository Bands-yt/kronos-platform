#pragma once

#include <glm/glm.hpp>
#include <volk.h>
#include <vk_mem_alloc.h>

#include "core/ECS.hpp"
#include "core/Mesh.hpp"
#include "core/ProceduralMaterials.hpp"

namespace engine::tntwars {

// Kronos bugfix (live-reported: "no physical TNT explosion" -- a placed
// charge had real, correct logical state in TntWarsMatch::tntCharges()
// the whole time, but zero visual representation: nothing at all sat on
// the ground between a real, accepted placeTntCharge() and its own real
// detonation several seconds later). A real, small, bright emissive box
// -- reads as a live TNT charge at a glance -- with no physics collider
// (a charge is a real visual-only marker; the player's own real raycast-
// placement already found a valid surface, it doesn't need to re-collide
// with its own charge).

[[nodiscard]] core::EntityId spawnTntChargeVisual(core::ECS& ecs, core::MeshLibrary& meshLibrary,
                                                     const core::ProceduralMaterialLibrary& materials,
                                                     VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool,
                                                     VkQueue queue, glm::vec3 position, const char* name);

} // namespace engine::tntwars
