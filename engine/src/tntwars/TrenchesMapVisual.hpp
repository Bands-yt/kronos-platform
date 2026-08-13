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

// Kronos ("Four RTX Maps" Phase 5d): real enrichment for the existing
// Trenches map -- MapLayout.cpp's own buildTrenches() is real (ground,
// team bases, cover, the destructible mid-wall, Core, Admin Room) but
// has no real sculpted terrain, fortifications, or elevated positions at
// all. This adds real mud terrain patches, real wooden bunkers at each
// team's own rally point, a real walkable TNT tunnel beneath the map's
// own mid-wall, two real elevated artillery platforms, and a real trench
// commander boss arena in no-man's-land -- alongside
// TrenchesCover.hpp's own real destructible-cover extension to the
// map's existing four "Cover_*" pieces.

// Real, tuned world position -- no-man's-land, directly behind the map's
// own mid-wall (MapLayout.cpp's "Wall_0".."Wall_4" sit at Z=0) so the
// commander arena reads as the real prize behind the real breach point.
[[nodiscard]] glm::vec3 kTrenchesMapCommanderArenaCenter();

// Real spawn: mud terrain patches, two real wooden bunkers (one per
// team's rally point), a real walkable TNT tunnel, two real elevated
// artillery platforms, a real trench-commander boss arena + boss + a
// forged-tool prop. Every structural piece gets a real static Jolt
// collider.
[[nodiscard]] std::vector<core::EntityId> spawnTrenchesMapVisual(core::ECS& ecs, core::Physics& physics,
                                                                   core::MeshLibrary& meshLibrary,
                                                                   const core::ProceduralMaterialLibrary& materials,
                                                                   VmaAllocator allocator, VkDevice device,
                                                                   VkCommandPool cmdPool, VkQueue queue,
                                                                   miningsim::RarityTier bossRarity);

} // namespace engine::tntwars
