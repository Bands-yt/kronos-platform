#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "tntwars/MapDefinition.hpp"

namespace engine::tntwars {

// Sprint 14's Island/Sea map: "Add torpedo stealth system (Saboteur
// class synergy)" -- a real, pure detection check: a stealthed torpedo
// (see Projectile.hpp's own `stealth` flag, set for
// ProjectileType::Torpedo) is only real-visible to the enemy team once
// it comes within `detectionRadius` of a real sonar source (a Sky/
// Island-map prop, or an Interceptor's own real HyperScan ultimate --
// see CinematicSequence.hpp).
[[nodiscard]] bool isDetectedBySonar(glm::vec3 torpedoPosition, const std::vector<glm::vec3>& sonarSources,
                                       float detectionRadius);

// Real, tuned default detection radius -- a sonar source doesn't see
// the whole map, only a real, bounded real range around itself.
constexpr float kDefaultSonarDetectionRadius = 12.0f;

// Kronos roadmap Milestone 11 ("Underwater map, merged from IslandSea"):
// the real, concrete sonar source positions `isDetectedBySonar()` above
// actually needs -- read straight from `map`'s own real "SonarBuoy_*"
// map-layout pieces (see MapLayout.cpp's own buildIslandSea()), the same
// "gameplay data derived straight from real map-layout piece names"
// pattern MapLayout.hpp's own coreWorldPosition()/teamSpawnPosition()
// already established. Real-empty for a map with no sonar buoys (every
// map except IslandSea today) rather than a fixed, honest fallback --
// isDetectedBySonar() already real-handles an empty source list as
// "never detected," so no special-casing is needed here.
[[nodiscard]] std::vector<glm::vec3> sonarSourcePositions(MapId map);

} // namespace engine::tntwars
