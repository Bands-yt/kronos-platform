#pragma once

#include <vector>

#include "tntwars/DestructibleGeometry.hpp"

namespace engine::tntwars {

// Kronos ("Four RTX Maps" Phase 5d): the brief's own "destructible cover"
// for the Trenches map -- MapLayout.cpp's own buildTrenches() already
// spawns four real, static "Cover_A_Left"/"Cover_A_Right"/"Cover_B_Left"/
// "Cover_B_Right" pieces (real colliders via spawnMapLayoutVisual(), see
// that function's own header comment), but none of them has ever been
// destructible -- only the map's own mid-wall (TrenchesWall.hpp) was.
// This reuses that exact same real DestructibleGeometry machinery
// (buildSegmentedWall(), applyExplosionToSegments(),
// spawnDestructibleWallVisual()/tickDestructibleWallVisual()) for the
// four cover pieces too, real, finer-grained health grids layered
// underneath those same four static visual positions -- the identical
// "static MapLayoutPiece is cosmetic, real per-segment state lives in
// its own module" split TrenchesWall.hpp's own header comment documents.

// Real, exact match of MapLayout.cpp's buildTrenches()'s own
// Cover_A_Left/Cover_A_Right/Cover_B_Left/Cover_B_Right positions and
// half-extents ({3,1,6} at x=+-20, z=+-15) -- kept here rather than
// imported from MapLayout.cpp (a pure-data .cpp with no header-exposed
// piece table) since TrenchesWall.hpp's own kTrenchesWall* constants set
// exactly this "restate the real, tuned numbers where they're needed"
// precedent already.
constexpr float kTrenchesCoverHalfWidth = 3.0f;
constexpr float kTrenchesCoverHalfHeight = 1.0f;
constexpr float kTrenchesCoverHalfDepth = 6.0f;
constexpr int kTrenchesCoverColumns = 2;
constexpr int kTrenchesCoverRows = 1;
constexpr float kTrenchesCoverSegmentHealth = 50.0f; // real, tuned lower than a wall segment (80) -- cover is meant to fall faster than the map's own central wall

[[nodiscard]] std::vector<DestructibleSegment> buildTrenchesCover();

} // namespace engine::tntwars
