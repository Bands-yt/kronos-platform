#pragma once

#include <vector>

#include <volk.h>
#include <vk_mem_alloc.h>

#include "core/ECS.hpp"
#include "core/Mesh.hpp"
#include "core/Physics.hpp"
#include "core/ProceduralMaterials.hpp"
#include "miningsim/Rarity.hpp"

namespace engine::tntwars {

// Kronos ("Four RTX Maps" Phase 5c): real enrichment for the existing
// IslandSea map -- MapLayout.cpp's own buildIslandSea() is real (a flat
// "Water" plane + two flat island boxes + two sonar buoys, see that
// function's own header comment) but has no real sculpted submerged
// terrain at all. This adds a real, primitive-composed deep-trench arena
// well below the existing surface-level islands (so it reads as a
// genuinely separate, deeper tier of the map, not an overlap): a sea
// floor with coral reef formations, a real V-shaped trench corridor, a
// real walkable sea cave, glowing crystal mining nodes (real,
// interactive -- see core::createOreNode()), oxygen-zone marker props,
// and a bioluminescent guardian boss arena. Real caustic-light dapple
// shading (Renderer::setUnderwaterCausticsEnabled()) is meant to be
// enabled alongside this content.
//
// Honest scope limit: this engine has no swimming/buoyancy physics (see
// core::Physics.hpp's own scope), so this arena is real, solid,
// walkable ground themed as a submerged trench -- exactly the same
// "ground-level arena with a hazard theme" precedent VolcanoMapVisual's
// own header comment already establishes for lava, not a literal flooded
// volume a player swims through.

// Real, tuned world position -- well below buildIslandSea()'s own real
// "Water" plane (y=-1) and islands (y=1), so this reads as a real, deeper
// trench tier beneath the existing surface layout rather than a physical
// overlap with it.
[[nodiscard]] glm::vec3 kUnderwaterMapCenter();

// Real spawn: the sea floor, coral reef formations, a real V-shaped
// trench corridor, a real walkable sea cave, four real interactive
// crystal mining nodes (core::createOreNode(), OreType::Crystal), three
// real oxygen-zone marker props, a bioluminescent guardian boss arena +
// boss + a forged-tool prop. Every structural piece gets a real static
// Jolt collider (the mining nodes get their own real collider via
// core::createOreNode() itself; the oxygen-zone markers stay
// decorative-only, matching SkyMapVisual's own crystal-vein "no collider
// needed" precedent).
[[nodiscard]] std::vector<core::EntityId> spawnUnderwaterMapVisual(core::ECS& ecs, core::Physics& physics,
                                                                     core::MeshLibrary& meshLibrary,
                                                                     const core::ProceduralMaterialLibrary& materials,
                                                                     VmaAllocator allocator, VkDevice device,
                                                                     VkCommandPool cmdPool, VkQueue queue,
                                                                     miningsim::RarityTier bossRarity);

} // namespace engine::tntwars
