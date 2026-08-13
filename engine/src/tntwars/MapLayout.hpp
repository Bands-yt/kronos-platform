#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "tntwars/MapDefinition.hpp"
#include "tntwars/Team.hpp"

namespace engine::tntwars {

// What shape a real spawned MapLayoutPiece becomes -- deliberately just
// the two primitives core::Mesh already exports factories for
// (createBox/createPlane) that a static level piece actually needs; no
// GPU/Vulkan dependency in this module (see class comment below).
enum class MapPieceShape { Box, Plane };

// One real, deterministic piece of a TNT-Wars map's static level
// geometry -- ground, team bases, cover, or a per-map hazard feature
// (e.g. Mantle's lava plane). Pure data, no mesh/GPU handles: this
// module has no Vulkan dependency on purpose, so buildMapLayout() is
// real, headless-testable logic, the same "pure data the render/ECS
// layer turns into real entities" split net::InteractionValidation.hpp
// and Projectile.hpp already draw. Studio's TntWarsPlugin is the real
// consumer -- it turns each piece into a real ECS entity (Transform +
// Renderable + MeshSource) via core::Mesh::createBox()/createPlane(),
// mirroring plugins::PrefabPlugin::spawnInstance()'s own shape.
struct MapLayoutPiece {
    std::string name;
    MapPieceShape shape = MapPieceShape::Box;
    glm::vec3 position{0.0f};
    // Box: real half-extents on all three axes. Plane: only x (half
    // width) and z (half depth) are read, matching core::Mesh::createPlane's
    // own (halfWidth, halfDepth) signature.
    glm::vec3 halfExtents{1.0f};
    glm::vec3 color{0.6f, 0.6f, 0.6f};
};

// Real, deterministic, per-map static geometry layout: a play surface,
// two opposing team bases (TeamA centered at -Z, TeamB at +Z, matching
// the brief's own "two-base artillery destruction" framing), symmetric
// cover, and real hazard/feature geometry driven directly off that map's
// own real MapModifiers (see MapDefinition.hpp) -- e.g. Mantle's layout
// includes a real lava-plane piece because mapModifiersFor(Mantle)
// .hasLavaHazard is true, not because this function re-decides that
// independently. Not hand-authored art -- real, honest procedural
// primitives, the same "real procedural over stylized" precedent
// core::spawnRiggedAvatar() already set for this engine.
[[nodiscard]] std::vector<MapLayoutPiece> buildMapLayout(MapId map);

// Kronos roadmap Milestone 1 ("team-aware spawn/match setup"): the real
// world-space point `team`'s players should spawn at on `map` -- reads it
// straight from that map's own real "TeamA_Base"/"TeamB_Base" piece
// (every real map layout has both, see buildMapLayout()'s own per-map
// builders), offset upward by a real, fixed height so a spawned player
// stands on top of the base platform rather than inside it. This is the
// first real gameplay consumer of those two pieces' positions -- until
// now they were pure level-decoration geometry with no code ever reading
// their own transform back out.
[[nodiscard]] glm::vec3 teamSpawnPosition(MapId map, TeamId team);

// Kronos roadmap Milestone 3 ("TNT/explosive core mechanic"): the real
// world-space point `team`'s own Core (see TntWarsMatch::coreHealth())
// sits at on `map` -- real-reuses the same "TeamA_Base"/"TeamB_Base"
// piece teamSpawnPosition() reads, but returns the base's own real
// center directly (no upward stand-off): the Core sits ON its team's
// base, distinct from where a player stands. Same real "every map has
// both bases, so the fallback is never actually taken" honesty as
// teamSpawnPosition() above.
[[nodiscard]] glm::vec3 coreWorldPosition(MapId map, TeamId team);

} // namespace engine::tntwars
