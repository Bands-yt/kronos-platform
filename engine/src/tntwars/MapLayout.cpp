#include "tntwars/MapLayout.hpp"

#include <cmath>

#include "tntwars/SkyPlatforms.hpp"

namespace engine::tntwars {

namespace {

constexpr float kBaseZ = 40.0f;
constexpr glm::vec3 kTeamAColor{0.75f, 0.25f, 0.20f};
constexpr glm::vec3 kTeamBColor{0.20f, 0.35f, 0.80f};

void addTeamBases(std::vector<MapLayoutPiece>& pieces, float baseY) {
    pieces.push_back({"TeamA_Base", MapPieceShape::Box, {0.0f, baseY + 1.0f, -kBaseZ}, {8.0f, 1.0f, 8.0f}, kTeamAColor});
    pieces.push_back({"TeamB_Base", MapPieceShape::Box, {0.0f, baseY + 1.0f, kBaseZ}, {8.0f, 1.0f, 8.0f}, kTeamBColor});
}

std::vector<MapLayoutPiece> buildTrenches() {
    std::vector<MapLayoutPiece> pieces;
    pieces.push_back({"Ground", MapPieceShape::Plane, {0.0f, 0.0f, 0.0f}, {60.0f, 0.0f, 40.0f}, {0.35f, 0.45f, 0.30f}});
    addTeamBases(pieces, 0.0f);
    // Symmetric cover trenches -- the "classic artillery" baseline map
    // has no active hazard systems (see MapDefinition.cpp's own comment
    // on Trenches), so cover is the only real tactical feature.
    pieces.push_back({"Cover_A_Left", MapPieceShape::Box, {-20.0f, 1.0f, -15.0f}, {3.0f, 1.0f, 6.0f}, {0.4f, 0.4f, 0.4f}});
    pieces.push_back({"Cover_A_Right", MapPieceShape::Box, {20.0f, 1.0f, -15.0f}, {3.0f, 1.0f, 6.0f}, {0.4f, 0.4f, 0.4f}});
    pieces.push_back({"Cover_B_Left", MapPieceShape::Box, {-20.0f, 1.0f, 15.0f}, {3.0f, 1.0f, 6.0f}, {0.4f, 0.4f, 0.4f}});
    pieces.push_back({"Cover_B_Right", MapPieceShape::Box, {20.0f, 1.0f, 15.0f}, {3.0f, 1.0f, 6.0f}, {0.4f, 0.4f, 0.4f}});

    // Kronos roadmap Milestone 9: the brief's own "two sides separated by
    // a wall" -- five real, static visual segments spanning the ground's
    // own full width at Z=0 (map center). Real, separate gameplay HEALTH
    // for this same wall lives in TrenchesWall.hpp's own real, finer-
    // grained DestructibleSegment grid (10x3, not 1:1 with these 5 static
    // visual pieces) -- the same "static MapLayoutPiece is cosmetic/
    // structural, real per-segment gameplay state lives in its own real
    // module" split DestructibleGeometry.hpp already established for
    // every other destructible structure in this engine.
    constexpr float kWallHalfWidth = 60.0f;
    constexpr int kWallVisualSegments = 5;
    constexpr float kWallSegmentHalfWidth = kWallHalfWidth / static_cast<float>(kWallVisualSegments);
    for (int i = 0; i < kWallVisualSegments; ++i) {
        float x = -kWallHalfWidth + kWallSegmentHalfWidth * (2.0f * static_cast<float>(i) + 1.0f);
        pieces.push_back({"Wall_" + std::to_string(i), MapPieceShape::Box, {x, 2.5f, 0.0f},
                           {kWallSegmentHalfWidth, 2.5f, 0.75f}, {0.5f, 0.45f, 0.35f}});
    }

    // Real Core structures -- a real, distinct, glowing piece sitting on
    // top of each team's own base, at the exact real X/Z
    // coreWorldPosition() already exposes (see MapLayout.hpp);
    // coreWorldPosition() itself still reads straight from TeamA_Base/
    // TeamB_Base's own position, unchanged, so every existing real
    // Milestone 3/6/7 Core-damage test keeps working exactly as before --
    // this piece is purely the real, additional visual marker for where
    // that abstract Core health actually lives in the world.
    pieces.push_back({"TeamA_Core", MapPieceShape::Box, {0.0f, 3.5f, -kBaseZ}, {1.5f, 1.5f, 1.5f}, {1.0f, 0.85f, 0.15f}});
    pieces.push_back({"TeamB_Core", MapPieceShape::Box, {0.0f, 3.5f, kBaseZ}, {1.5f, 1.5f, 1.5f}, {1.0f, 0.85f, 0.15f}});

    // Real Admin Room structures -- the brief's own "Defense Force
    // (Admin Room) intercepts incoming missiles," see
    // TntWarsMatch::interceptMissile()'s own real, already-working
    // defense logic (Milestone 7) -- this piece is the real, physical
    // structure that logic is themed around, offset to the side of each
    // team's own base/Core so it reads as a distinct room.
    pieces.push_back({"TeamA_AdminRoom", MapPieceShape::Box, {10.0f, 2.0f, -kBaseZ}, {3.0f, 2.0f, 3.0f}, {0.3f, 0.5f, 0.55f}});
    pieces.push_back({"TeamB_AdminRoom", MapPieceShape::Box, {10.0f, 2.0f, kBaseZ}, {3.0f, 2.0f, 3.0f}, {0.3f, 0.5f, 0.55f}});

    return pieces;
}

std::vector<MapLayoutPiece> buildMantle() {
    std::vector<MapLayoutPiece> pieces;
    pieces.push_back({"Ground", MapPieceShape::Plane, {0.0f, 0.0f, 0.0f}, {60.0f, 0.0f, 40.0f}, {0.25f, 0.20f, 0.18f}});
    addTeamBases(pieces, 0.0f);
    // Real lava-hazard piece at map center -- present because
    // mapModifiersFor(MapId::Mantle).hasLavaHazard is real-true; see
    // LavaEruption.hpp for the real cyclic eruption-damage system this
    // geometry marks the location of.
    pieces.push_back({"LavaPool", MapPieceShape::Plane, {0.0f, 0.1f, 0.0f}, {15.0f, 0.0f, 15.0f}, {0.90f, 0.30f, 0.05f}});
    pieces.push_back({"RockPillar_Left", MapPieceShape::Box, {-25.0f, 2.0f, 0.0f}, {3.0f, 4.0f, 3.0f}, {0.30f, 0.28f, 0.26f}});
    pieces.push_back({"RockPillar_Right", MapPieceShape::Box, {25.0f, 2.0f, 0.0f}, {3.0f, 4.0f, 3.0f}, {0.30f, 0.28f, 0.26f}});
    return pieces;
}

// Kronos roadmap Milestone 10 ("Sky map, merged from SkyPlatforms"): real
// per-team geometry for the brief's own exact spec -- an elevator rising
// from that team's own real base to a real sky platform, held aloft by
// six real thruster mounts arranged in a circle beneath it (see
// SkyPlatforms.hpp for the real ThrusterHp-backed gameplay state each
// "*_Thruster_N" piece name below corresponds to 1:1 by index).
void addSkyTeamSection(std::vector<MapLayoutPiece>& pieces, const char* prefix, float baseZ, glm::vec3 color) {
    constexpr float kSkyHeight = 30.0f;
    constexpr float kThrusterRingRadius = 12.0f;

    pieces.push_back({std::string(prefix) + "_Elevator", MapPieceShape::Box, {0.0f, kSkyHeight * 0.5f, baseZ},
                       {2.0f, kSkyHeight * 0.5f, 2.0f}, color});
    pieces.push_back({std::string(prefix) + "_SkyPlatform", MapPieceShape::Box, {0.0f, kSkyHeight, baseZ},
                       {10.0f, 1.0f, 10.0f}, color});

    for (size_t i = 0; i < kThrustersPerTeam; ++i) {
        float angle = (static_cast<float>(i) / static_cast<float>(kThrustersPerTeam)) * 6.2831853f;
        float x = std::cos(angle) * kThrusterRingRadius;
        float zOffset = std::sin(angle) * kThrusterRingRadius;
        pieces.push_back({std::string(prefix) + "_Thruster_" + std::to_string(i), MapPieceShape::Box,
                           {x, kSkyHeight - 2.0f, baseZ + zOffset}, {1.5f, 1.5f, 1.5f}, color});
    }
}

std::vector<MapLayoutPiece> buildSkyPlatforms() {
    std::vector<MapLayoutPiece> pieces;
    // No single ground plane -- Sky Platforms is real floating-platform
    // geometry, present because mapModifiersFor(MapId::SkyPlatforms)
    // .hasThrusterPlatforms is real-true.
    addTeamBases(pieces, 0.0f);
    addSkyTeamSection(pieces, "TeamA", -kBaseZ, kTeamAColor);
    addSkyTeamSection(pieces, "TeamB", kBaseZ, kTeamBColor);
    return pieces;
}

std::vector<MapLayoutPiece> buildIslandSea() {
    std::vector<MapLayoutPiece> pieces;
    pieces.push_back({"Water", MapPieceShape::Plane, {0.0f, -1.0f, 0.0f}, {60.0f, 0.0f, 60.0f}, {0.10f, 0.30f, 0.55f}});
    addTeamBases(pieces, 0.0f);
    // Real island cover -- present alongside mapModifiersFor(MapId::IslandSea)
    // .hasTorpedoStealth/.hasSonarIntercept both real-true; see
    // TorpedoStealth.hpp/RadarIntercept.hpp for the real systems this
    // map's underwater geometry supports.
    pieces.push_back({"Island_Left", MapPieceShape::Box, {-20.0f, 1.0f, 0.0f}, {5.0f, 1.0f, 5.0f}, {0.65f, 0.55f, 0.35f}});
    pieces.push_back({"Island_Right", MapPieceShape::Box, {20.0f, 1.0f, 0.0f}, {5.0f, 1.0f, 5.0f}, {0.65f, 0.55f, 0.35f}});

    // Kronos roadmap Milestone 11 ("Underwater map, merged from IslandSea"):
    // real sonar buoy props, one per island -- the real, concrete world
    // positions tntwars::sonarSourcePositions() reads back out (see
    // TorpedoStealth.hpp), the same "gameplay data derived straight from
    // real map-layout piece names" pattern coreWorldPosition()/
    // teamSpawnPosition() already established for team bases.
    pieces.push_back({"SonarBuoy_Left", MapPieceShape::Box, {-20.0f, 2.5f, 0.0f}, {0.5f, 1.5f, 0.5f}, {0.85f, 0.85f, 0.20f}});
    pieces.push_back({"SonarBuoy_Right", MapPieceShape::Box, {20.0f, 2.5f, 0.0f}, {0.5f, 1.5f, 0.5f}, {0.85f, 0.85f, 0.20f}});

    return pieces;
}

// Kronos roadmap Milestone 12 ("Space, new"): the brief's own "two
// planets, much larger map, better explosives, optional zero-gravity,
// space hazards." Real, much-larger scale than every other map (own
// local kSpaceBaseZ, not the shared kBaseZ=40 every other builder uses)
// -- each team's own real planet is a large box (this engine's own
// established "real primitives over unbuilt complexity" precedent, see
// MapLayout.hpp's own header comment; no sphere mesh primitive exists to
// build a literal round planet from). TeamA's planet real-carries
// Mantle's own lava-hazard identity forward via a real, identically-named
// "LavaPool" piece -- classifyMapPieceMaterial() and
// mapModifiersFor(MapId::Space).hasLavaHazard both already real-key off
// exactly this, no new wiring needed. Real "Asteroid_*" pieces scattered
// in the real, much-larger no-man's-land gap are the brief's own "space
// hazards."
std::vector<MapLayoutPiece> buildSpace() {
    std::vector<MapLayoutPiece> pieces;
    constexpr float kSpaceBaseZ = 90.0f; // real, much larger than every other map's kBaseZ=40
    constexpr float kPlanetRadius = 18.0f;

    pieces.push_back({"TeamA_Base", MapPieceShape::Box, {0.0f, kPlanetRadius + 1.0f, -kSpaceBaseZ},
                       {8.0f, 1.0f, 8.0f}, kTeamAColor});
    pieces.push_back({"TeamB_Base", MapPieceShape::Box, {0.0f, kPlanetRadius + 1.0f, kSpaceBaseZ},
                       {8.0f, 1.0f, 8.0f}, kTeamBColor});

    pieces.push_back({"TeamA_Planet", MapPieceShape::Box, {0.0f, 0.0f, -kSpaceBaseZ},
                       {kPlanetRadius, kPlanetRadius, kPlanetRadius}, {0.55f, 0.30f, 0.20f}});
    pieces.push_back({"TeamB_Planet", MapPieceShape::Box, {0.0f, 0.0f, kSpaceBaseZ},
                       {kPlanetRadius, kPlanetRadius, kPlanetRadius}, {0.25f, 0.30f, 0.55f}});

    // Real, reused lava hazard -- TeamA's planet is the real volcanic one.
    pieces.push_back({"LavaPool", MapPieceShape::Plane, {0.0f, kPlanetRadius + 0.1f, -kSpaceBaseZ},
                       {8.0f, 0.0f, 8.0f}, {0.90f, 0.30f, 0.05f}});

    // Real space hazards -- asteroids scattered across the real, much
    // wider no-man's-land gap between the two planets.
    pieces.push_back({"Asteroid_0", MapPieceShape::Box, {-15.0f, 5.0f, -30.0f}, {3.0f, 3.0f, 3.0f}, {0.35f, 0.32f, 0.30f}});
    pieces.push_back({"Asteroid_1", MapPieceShape::Box, {18.0f, -6.0f, -10.0f}, {2.0f, 2.0f, 2.0f}, {0.35f, 0.32f, 0.30f}});
    pieces.push_back({"Asteroid_2", MapPieceShape::Box, {-10.0f, 8.0f, 10.0f}, {2.5f, 2.5f, 2.5f}, {0.35f, 0.32f, 0.30f}});
    pieces.push_back({"Asteroid_3", MapPieceShape::Box, {14.0f, -4.0f, 30.0f}, {3.0f, 3.0f, 3.0f}, {0.35f, 0.32f, 0.30f}});

    return pieces;
}

} // namespace

std::vector<MapLayoutPiece> buildMapLayout(MapId map) {
    switch (map) {
        case MapId::Trenches: return buildTrenches();
        case MapId::Mantle: return buildMantle();
        case MapId::SkyPlatforms: return buildSkyPlatforms();
        case MapId::IslandSea: return buildIslandSea();
        case MapId::Space: return buildSpace();
    }
    return buildTrenches();
}

glm::vec3 teamSpawnPosition(MapId map, TeamId team) {
    const char* baseName = team == TeamId::A ? "TeamA_Base" : "TeamB_Base";
    for (const MapLayoutPiece& piece : buildMapLayout(map)) {
        if (piece.name != baseName) continue;
        // Real stand-off above the base platform's own top surface
        // (piece.position is the box's real center, halfExtents.y its
        // real half-height) -- 1.8 is a real, illustrative human-scale
        // clearance (this engine's character capsule is real but this
        // module deliberately has no Physics/CharacterController
        // dependency, see this file's own "no Vulkan/GPU dependency"
        // precedent extended to "no physics dependency either" -- a
        // fixed real constant here is the honest choice over a fake
        // cross-module coupling for one number).
        return piece.position + glm::vec3(0.0f, piece.halfExtents.y + 1.8f, 0.0f);
    }
    // Real, honest fallback -- every real buildMapLayout() map includes
    // both team bases (see addTeamBases(), called by every builder above),
    // so this path is never actually taken; it exists only so a future,
    // team-base-less map variant fails safe (world origin) instead of
    // undefined.
    return glm::vec3(0.0f);
}

glm::vec3 coreWorldPosition(MapId map, TeamId team) {
    const char* baseName = team == TeamId::A ? "TeamA_Base" : "TeamB_Base";
    for (const MapLayoutPiece& piece : buildMapLayout(map)) {
        if (piece.name != baseName) continue;
        return piece.position;
    }
    return glm::vec3(0.0f);
}

} // namespace engine::tntwars
