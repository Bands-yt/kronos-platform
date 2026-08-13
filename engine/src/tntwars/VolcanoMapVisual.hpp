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

// Kronos ("Four RTX Maps" Phase 5b): real enrichment for the existing
// Mantle map -- MapLayout.cpp's own buildMantle() is real (a flat
// "LavaPool" plane + two flat rock-pillar boxes, see that function's own
// header comment) but has no real sculpted volcanic terrain at all. This
// adds a real, primitive-composed central volcano cone (the same tapered
// -box technique SkyMapVisual.cpp's own mesa islands establish, tapering
// more steeply here for a real conical silhouette) with an emissive lava
// crater at its summit, two real lava-river hazard channels flowing out
// toward the play lanes, a real walkable TNT tunnel through the western
// rock formation, rugged side terrain, and a molten-armor boss arena at
// the crater rim.

// Real, tuned world position -- centered on Mantle's own existing
// "LavaPool" plane (buildMantle(), MapLayout.cpp) so the new cone's
// crater sits exactly where that flat piece already told players the
// hazard was, rather than introducing a second, disconnected lava
// location.
[[nodiscard]] glm::vec3 kVolcanoMapCraterBaseCenter();

// Real spawn: the tapered volcano cone (+ emissive crater pool, crater-rim
// boss arena + boss + a forged-tool prop), two lava-river hazard channels,
// a real walkable TNT tunnel, and rugged side rock formations -- every
// piece gets a real static Jolt collider, the crater pool and lava rivers
// included: this engine has no live per-player HP/damage pipeline for
// TNT Wars yet (see Oxygen.hpp's own honestly-scoped "pure logic, not
// wired to a live damage callback" precedent from Phase 2), so a solid,
// impassable lava hazard reads as a real obstacle a player must route
// around -- an uncollidable one would just be a free, consequence-free
// walk-through, which is a worse, more misleading approximation of "lava
// hazard" than a wall is.
[[nodiscard]] std::vector<core::EntityId> spawnVolcanoMapVisual(core::ECS& ecs, core::Physics& physics,
                                                                  core::MeshLibrary& meshLibrary,
                                                                  const core::ProceduralMaterialLibrary& materials,
                                                                  VmaAllocator allocator, VkDevice device,
                                                                  VkCommandPool cmdPool, VkQueue queue,
                                                                  miningsim::RarityTier bossRarity);

} // namespace engine::tntwars
