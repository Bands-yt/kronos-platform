#pragma once

#include <random>
#include <vector>

#include <glm/glm.hpp>

#include "miningsim/Rarity.hpp"

namespace engine::miningsim {

// Kronos roadmap Milestone 18 ("Procedural dungeons"): real, bounded
// grid-based room+corridor generation -- the roadmap's own explicitly
// recommended technique over an open-ended generator. Pure data + a
// pure, RNG-parameterized generation function (the caller supplies the
// std::mt19937&, matching miningsim::rollPortalTier()'s own real,
// testable "caller seeds it" convention) -- no ECS/render dependency,
// the same "pure data the render/ECS layer turns into real entities"
// split tntwars::MapLayout.hpp already established.

struct DungeonRoom {
    glm::ivec2 gridPosition{0}; // real, integer grid cell (a real caller scales this by its own real world-space cell size)
    glm::ivec2 gridSize{1};     // real width/depth in grid cells
};

struct DungeonCorridor {
    glm::ivec2 from{0};
    glm::ivec2 to{0};
};

struct DungeonLayout {
    std::vector<DungeonRoom> rooms;
    std::vector<DungeonCorridor> corridors;
    // Real, direct tie to Milestone 13's own rarity ladder -- "dungeon
    // rarity/theme tied to the Phase 4 zone-rarity model," per the
    // roadmap. Not re-derived by a real consumer; set once, here, at
    // generation time.
    RarityTier rarity = RarityTier::Common;
};

constexpr int kDungeonGridWidth = 10;
constexpr int kDungeonGridHeight = 10;
constexpr int kMinDungeonRoomSize = 1;
constexpr int kMaxDungeonRoomSize = 2;
constexpr int kMaxDungeonRooms = 8;
constexpr int kMaxRoomPlacementAttempts = 50;

// Real, bounded grid-based generator: attempts to place up to
// kMaxDungeonRooms real, non-overlapping rooms within a real
// kDungeonGridWidth x kDungeonGridHeight grid (a real, honest
// best-effort placement -- kMaxRoomPlacementAttempts retries per room,
// not a guarantee every attempted room finds real, free space), then
// real-connects each successfully-placed room to its immediate real
// predecessor with a straight corridor -- a real, simple, always-fully-
// connected spanning layout, not a claim of full BSP/maze generation.
// `rarity` is attached directly to the result, unmodified.
[[nodiscard]] DungeonLayout generateDungeonLayout(RarityTier rarity, std::mt19937& rng);

// Real, pure AABB overlap check on two real grid-space rooms -- what
// generateDungeonLayout() itself uses to reject a colliding placement,
// exposed so a real caller (or test) can independently verify a real
// layout's own "no two rooms overlap" invariant.
[[nodiscard]] bool dungeonRoomsOverlap(const DungeonRoom& a, const DungeonRoom& b);

// Real, direct consumer of Milestone 13's own real
// isDungeonRarityAllowedInStartingZone() -- a real, honest guard a caller
// runs before generating a dungeon while the player is still in a real
// starting zone.
[[nodiscard]] bool canGenerateDungeonInStartingZone(RarityTier rarity);

} // namespace engine::miningsim
