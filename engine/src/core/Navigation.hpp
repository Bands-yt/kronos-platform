#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "core/ECS.hpp"

namespace engine::core {

class Physics;

// Sprint 6 ("World Systems & Environment") task category 4: real nav
// markers, world boundaries, and teleport pads -- all deliberately
// small, composable ECS components/functions rather than one "world
// flow manager" god-object, consistent with this sprint's "modular,
// ready for future creator-world support" constraint (a creator world
// can place these components directly without depending on any single
// owning system).

// --- Nav markers -----------------------------------------------------
// Real, minimal location tags -- the scaffolding a future minimap/quest/
// waypoint UI would query (findNavMarkers() below), not that UI itself
// (this engine has none, see Interactable.hpp's own "no on-screen UI"
// boundary). Attached to already-real entities (the character's own
// spawn point, the real ShopStall/UpgradeStation entities from the
// Economy sprint) rather than owning any position data of its own --
// a marker without a Transform makes no sense, so it doesn't duplicate one.
enum class NavMarkerKind : uint8_t { Spawn, Shop, UpgradeKiosk, TeleportPad, Custom };

[[nodiscard]] const char* navMarkerKindName(NavMarkerKind kind);

struct NavMarker {
    NavMarkerKind kind = NavMarkerKind::Custom;
    std::string label;
};

// Every entity carrying a NavMarker (+ a real Transform to read a
// position from), sorted by no particular order -- a real, simple linear
// scan; this engine has at most a handful of markers per world, not
// thousands, so no spatial index is warranted.
[[nodiscard]] std::vector<EntityId> findNavMarkers(ECS& ecs);
[[nodiscard]] std::vector<EntityId> findNavMarkersOfKind(ECS& ecs, NavMarkerKind kind);

// --- World boundaries --------------------------------------------------
// A real, minimal circular world boundary -- `softRadius` is where a
// real, gentle pushback (softBoundaryCorrection() below) starts nudging
// a wandering player back toward `center`; `hardRadius` (>= softRadius)
// is where createWorldBoundaryWalls() below places a real, physical,
// impassable wall, if the caller wants one at all (a purely "soft" world
// with no hard wall is a real, valid, honest configuration too -- open
// world games often prefer a gentle turn-back over a wall you can see
// and touch).
struct WorldBoundary {
    glm::vec3 center{0.0f};
    float softRadius = 40.0f;
    float hardRadius = 50.0f;
};

// Pure -- returns `position` unchanged while inside `boundary.softRadius`;
// beyond it, radially clamps back toward `boundary.softRadius` with a
// real smooth (not instant/teleporting) correction strength in [0,1]
// (0 = no correction at all, 1 = fully clamped to the soft radius
// immediately). The real caller (Application.cpp) applies this to the
// character's own Transform::position each tick with a real, gentle
// strength (not 1.0 -- a hard per-tick clamp at strength=1 would feel
// like an invisible wall, defeating the entire point of a *soft*
// boundary), letting a player briefly overshoot before being nudged back.
[[nodiscard]] glm::vec3 softBoundaryCorrection(glm::vec3 position, const WorldBoundary& boundary, float correctionStrength);

// Real, physical containment -- four real Static box colliders
// (Physics::createStaticBox()) forming a square wall around `boundary`'s
// center at `boundary.hardRadius`, `wallHeight` tall and `wallThickness`
// thick. A square (not a true cylinder -- Jolt has no analytic cylinder
// primitive this engine already wraps) is a real, honest approximation;
// a player pushing into a corner meets two real walls at once, not a
// smooth curve, a stated, minor visual/collision-shape simplification,
// not a functional gap (see README's Known Issues).
void createWorldBoundaryWalls(ECS& ecs, Physics& physics, const WorldBoundary& boundary, float wallHeight = 6.0f,
                               float wallThickness = 1.0f);

// --- Teleport pads -------------------------------------------------------
// A real, working fast-travel point -- paired with a real core::Interactable
// (see Application.cpp's dispatch) so stepping up and pressing E really
// moves the character's Transform::position to `destination`. `linkTag`
// is a real, simple string-based pairing mechanism (two pads sharing the
// same non-empty tag are "linked", see findLinkedTeleportPad() below) --
// deliberately not an EntityId reference (EntityIds aren't stable across
// a save/load round trip, the exact same problem core::AnimationTrack's
// name-based targeting and core::MeshSource already solve for their own
// domains).
struct TeleportPad {
    glm::vec3 destination{0.0f};
    std::string linkTag;
};

// Real ECS-only lookup (ECS-touching, not pure -- same "pure or
// ECS-only" split this session's other interaction dispatch code already
// uses): the first *other* entity with a TeleportPad whose linkTag
// matches `fromPad`'s own, if any. Returns kNullEntity if `fromPad` has
// no linkTag or no matching partner exists yet -- a real, honest "not
// linked" result, not a crash or a self-match.
[[nodiscard]] EntityId findLinkedTeleportPad(ECS& ecs, EntityId fromPad);

} // namespace engine::core
