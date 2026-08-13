#pragma once

#include <vector>

#include "tntwars/DestructibleGeometry.hpp"

namespace engine::tntwars {

// Kronos roadmap Milestone 9 ("Trenches map extension"): the real,
// destructible mid-map wall the brief's own Trenches spec calls for --
// "two sides separated by a wall; missiles/explosives to break wall."
// Built via DestructibleGeometry's own real buildSegmentedWall(), the
// same "many small real Box entities, each with health" shape Milestone 2
// established generically, applied here to the one real map that
// actually has a wall today. Spatially matches MapLayout.cpp's own five
// real static "Wall_0".."Wall_4" visual pieces (same overall footprint:
// full width 120, height 5, thickness 1.5, centered at map Z=0) -- a
// real, finer-grained 10x3 health grid layered underneath those coarser
// static visuals, the same "static piece is cosmetic, real per-segment
// state lives in its own module" split every other destructible
// structure in this engine already follows.
constexpr float kTrenchesWallHalfWidth = 60.0f;
constexpr float kTrenchesWallHalfHeight = 2.5f;
constexpr float kTrenchesWallThickness = 0.75f;
constexpr int kTrenchesWallColumns = 10;
constexpr int kTrenchesWallRows = 3;
constexpr float kTrenchesWallSegmentHealth = 80.0f;

[[nodiscard]] std::vector<DestructibleSegment> buildTrenchesWall();

// Real, direct alias of isStructureFullyDestroyed() -- the wall is
// real-breached (fully passable) only once every real segment is gone;
// this engine deliberately doesn't model a partial "hole" as passable
// (that would need real per-segment line-of-sight/pathing logic this
// module has no dependency on), matching the same honest, bounded scope
// DestructibleGeometry.hpp's own segments already commit to.
[[nodiscard]] bool isTrenchesWallBreached(const std::vector<DestructibleSegment>& wall);

} // namespace engine::tntwars
