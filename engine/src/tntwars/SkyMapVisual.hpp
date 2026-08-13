#pragma once

#include <glm/glm.hpp>

namespace engine::tntwars {

// Kronos ("Sky Bases" world-building): the map's own real geometric
// center -- generateSkyIslandLayout()'s own `areaCenter` for the
// procedural island scatter, and the real midpoint the two Sky Bases
// (see SkyMapTerrain.hpp's own SkyBaseSide/spawnSkyBase()) sit
// symmetrically placed around (see main.cpp's own real call site). Well
// above both teams' existing kSkyHeight=30 platforms (see MapLayout.cpp's
// addSkyTeamSection()) so this whole real heightfield tier reads as a
// genuinely separate, higher layer of the map, not an overlap.
//
// Formerly also the anchor for a real, primitive-composed tapered-box
// "mesa" (spawnSkyMapVisual()) the player spawned directly on top of (a
// real, live-flagged "pillar," removed), and later a single shared
// "designed spawn point" with its own real forge boss arena
// (spawnSkyMapBossArena(), also removed -- that boss content was a real
// Mining Sim carry-over that never belonged in TNT Wars). Both are gone;
// only this real center-point constant remains.
[[nodiscard]] glm::vec3 kSkyMapCentralIslandCenter();

} // namespace engine::tntwars
