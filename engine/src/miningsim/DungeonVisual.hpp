#pragma once

#include <vector>

#include <glm/glm.hpp>
#include <volk.h>
#include <vk_mem_alloc.h>

#include "core/ECS.hpp"
#include "core/Mesh.hpp"
#include "core/ProceduralMaterials.hpp"
#include "miningsim/Dungeon.hpp"

namespace engine::miningsim {

// Kronos trailer production: the real, first ECS/render consumer of
// Milestone 18's own real generateDungeonLayout() -- turns its real
// grid-space rooms/corridors into real, spawned entities (real
// geometry, real generated PBR stone material, matching
// MiningSimRtx.cpp's own cavern material), not a hand-placed stand-in.
// Each room gets a real floor slab plus a real, rarity-colored emissive
// beacon pillar at its own center (rarityTierColor(layout.rarity)) so a
// filmed dungeon visibly reads as "this rarity" at a glance; each
// corridor is a real, thin connecting stone strip between room centers.
constexpr float kDungeonRoomFloorHalfHeight = 0.4f;
constexpr float kDungeonBeaconHalfHeight = 2.5f;
constexpr float kDungeonCorridorHalfWidth = 1.0f;
constexpr float kDungeonDefaultCellSize = 4.0f;

// `cellSize` is the real world-space size of one grid cell; `origin` is
// where grid cell (0,0) sits in world space (floor level, Y=0).
[[nodiscard]] std::vector<core::EntityId> spawnDungeonVisual(core::ECS& ecs, core::MeshLibrary& meshLibrary,
                                                              const core::ProceduralMaterialLibrary& materials,
                                                              VmaAllocator allocator, VkDevice device,
                                                              VkCommandPool cmdPool, VkQueue queue,
                                                              const DungeonLayout& layout, glm::vec3 origin,
                                                              float cellSize = kDungeonDefaultCellSize);

} // namespace engine::miningsim
