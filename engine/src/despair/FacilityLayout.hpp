#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "despair/InteractionSystem.hpp"
#include "despair/LootSystem.hpp"

namespace engine::despair {

// PROJECT: DESPAIR -- static geometry + gameplay-entity placement for the
// vertical slice's facility level, split the same pure/ECS-owning way
// housedemo::HouseLayout.hpp/HouseDemoScene.hpp already are:
// computeFacilityLayout() below is pure, headless, glm-only data (no
// ECS/Vulkan dependency, independently unit-testable); FacilityMapBuilder.hpp's
// buildFacilityScene() is the separate ECS/Physics/Vulkan-owning half that
// spawns real entities from this list, layering the InteractionSystem.hpp
// components (LootContainer, Keycard, LockedDoor, HidingSpot) and the
// three HorrorAIManager AI tiers on top -- the same "generic box list +
// hand-placed special entities" convention HouseDemoScene.cpp establishes
// for its own front door. Still no room-topology nav graph here:
// HorrorAIManager's Hunting/Investigating states always target the
// player's live or last-known position directly (see its own header
// comment on why no pathfinding solver exists anywhere in this engine).
// FacilityAiSpawnSpec::patrolWaypoints is not that graph either -- it's
// just a hand-authored, ordered list of real positions one specific
// Tormentor walks straight between (via the exact same directLineStep()
// every other AI movement already uses) while otherwise idle, so this
// layout does place a *few* extra positions now, but never a solved path
// through arbitrary space.

enum class FacilityWallKind { Floor, Wall };

// Unlike housedemo::HousePart, every FacilityWall gets a real
// Physics::createStaticBox collider in FacilityMapBuilder, not just a
// Renderable -- a facility's walls/floor have to actually block movement
// and occlude the core::Physics::raycast() line-of-sight checks
// SanitySystem's gaze detection and HorrorAIManager's hasLineOfSight both
// depend on, unlike HouseDemoScene's purely decorative walls.
struct FacilityWall {
    FacilityWallKind kind = FacilityWallKind::Wall;
    glm::vec3 localPosition{0.0f}; // facility-local space, origin at EntryHall's floor center, y=0 floor level
    glm::vec3 halfExtents{0.5f};
    glm::vec3 color{0.55f};
};

// A door leaf sitting in a wall gap this layout's own FacilityWall entries
// already cut out -- FacilityMapBuilder spawns it as a real core::Door +
// core::Interactable (+ despair::LockedDoor when `locked`), the same
// hand-built special-entity treatment HouseDemoScene.cpp gives its own
// front door. Matches core::Door's own documented scope (no physics hinge
// exists anywhere in this engine): the leaf carries no collider, so the
// keycard gate is a real, tracked game-state gate (LockedDoor::locked,
// checked by tryUnlockDoor()) rather than a physical one -- the same
// non-blocking-door precedent HouseDemoScene's own front door already
// establishes, not a new gap introduced here.
struct FacilityDoorSpec {
    glm::vec3 localPosition{0.0f};
    glm::vec3 scale{1.2f, 2.2f, 0.1f}; // full leaf size in meters (width, height, thickness) -- unit-box convention
    bool locked = false;
    KeycardTier requiredTier = KeycardTier::Red;
};

struct FacilityHidingSpotSpec {
    glm::vec3 localPosition{0.0f}; // where the locker/prop itself sits
    glm::vec3 interiorPosition{0.0f}; // facility-local; offset by worldOrigin same as localPosition, not by localPosition itself
};

// Which procedural greybox FacilityMapBuilder builds for a container's own
// visuals (see its addLockerGreyboxVisuals()/addDeskGreyboxVisuals()) --
// purely a render/geometry choice, never read by LootContainer's own
// mechanic (InteractionSystem.hpp) or by grantContainerLoot(). `None` keeps
// the original single scaled box -- the duffel bag's own soft-sided shape
// doesn't read as rigid "furniture" in greybox form the way a chest or a
// desk does, so it's left exactly as it always rendered.
enum class FacilityFurnitureKind { None, Locker, Desk };

// A duffel bag is just a LootContainer with a longer searchDurationSeconds
// than a quick footlocker -- see InteractionSystem.hpp's own comment.
// Deliberately no separate "kind" field.
struct FacilityContainerSpec {
    glm::vec3 localPosition{0.0f};
    float searchDurationSeconds = 3.0f;
    std::string prompt = "Hold E to search";
    FacilityFurnitureKind furniture = FacilityFurnitureKind::None;

    // What FacilityMapBuilder attaches as this container's own LootDrop
    // (LootSystem.hpp) -- LootKind::None (the default) is a real, honest
    // "searched it, found nothing" container, not an oversight.
    LootDrop loot;
};

struct FacilityKeycardSpec {
    glm::vec3 localPosition{0.0f};
    KeycardTier tier = KeycardTier::Red;
};

enum class FacilityAiTier { Stalker, Tormentor, Culler };

struct FacilityAiSpawnSpec {
    FacilityAiTier tier = FacilityAiTier::Stalker;
    // Feet-level -- AI entities are plain Transform-driven with no
    // capsule (see HorrorAIManager.hpp), unlike playerSpawn below, which
    // is capsule-center.
    glm::vec3 localPosition{0.0f};

    // Facility-local waypoints (converted to world space the same way
    // localPosition is, at spawn) for despair::TormentorAIState::patrolWaypoints
    // -- see that struct's own comment. Empty for every tier except the
    // one Tormentor this layout actually routes on a patrol; Stalker/Culler
    // spawns leave this unused (their own state structs carry no patrol
    // concept).
    std::vector<glm::vec3> patrolWaypoints;
};

// A single overhead fixture wired to the facility breaker (see
// despair::BreakerPoweredLight in EscapeGameLoop.hpp) rather than always-on
// like a generic core::Light -- the room stays dark until the player
// actually finds and activates the breaker, matching the "power box"
// interaction the vertical slice's showcase wing wants: flipping one real
// switch lighting up a room the player can see change, not just a
// recolored prop.
struct FacilityLightSpec {
    glm::vec3 localPosition{0.0f};
    glm::vec3 color{0.85f, 0.90f, 1.0f};
    float litIntensity = 1.4f;
    float radius = 6.0f;
};

// A pushable physical prop (core::WorldPropKind::Crate is a real Dynamic
// Jolt body -- see WorldProp.hpp/.cpp) placed in open floor space, never
// inside a doorway/corridor gap: this engine's character capsule is
// itself a real Dynamic Jolt body (see Physics::createCharacterCapsule()),
// so pushing a crate is genuine rigid-body contact resolution, not a
// scripted shove -- but nothing here can visually confirm a crate placed
// inside a 2m gap wouldn't wedge itself unpushably across the only path
// through, so this layout only ever places them in open room floor,
// where a mis-push can't soft-lock the level.
struct FacilityCratePropSpec {
    glm::vec3 localPosition{0.0f};
    glm::vec3 halfExtent{0.4f};
};

// The facility's own power breaker -- the second half (alongside the
// Master Keycard, a Gold-tier FacilityKeycardSpec/LootDrop) of the two-part
// gate EscapeGameLoop.hpp's blastDoor needs open. A real, honest toggle
// prop (core::Interactable + despair::PowerBreaker on the same entity),
// not a fabricated wire puzzle.
struct FacilityBreakerSpec {
    glm::vec3 localPosition{0.0f};
};

// The facility's exit -- the one door in this layout gated by
// EscapeGameLoop::tryUnlockExitWithObjective() (requires
// kKeycardsRequiredForExit keycards of any tier, via the player's own
// despair::ObjectiveManager) rather than plain tryUnlockDoor(), and whose
// successful unlock is this vertical slice's real win condition (see
// Application.cpp's own wiring), not just a generic toggleDoor(). Same
// unit-box leaf convention FacilityDoorSpec establishes. `requiredTier` is
// vestigial for this simplified showcase objective (unused by
// tryUnlockExitWithObjective(), which counts tiers, not tier-matches) --
// kept only because EscapeGameLoop.cpp's older tryEscapeThroughBlastDoor()
// still exists and is still exercised by its own tests.
struct FacilityBlastDoorSpec {
    glm::vec3 localPosition{0.0f};
    glm::vec3 scale{0.2f, 2.2f, 1.2f}; // full leaf size in meters -- matches FacilityDoorSpec's own convention
    KeycardTier requiredTier = KeycardTier::Gold;
};

struct FacilityLayout {
    std::vector<FacilityWall> geometry;
    std::vector<FacilityDoorSpec> doors;
    std::vector<FacilityHidingSpotSpec> hidingSpots;
    std::vector<FacilityContainerSpec> containers;
    std::vector<FacilityKeycardSpec> keycards;
    std::vector<FacilityAiSpawnSpec> aiSpawns;
    std::vector<FacilityBreakerSpec> breakers;
    std::vector<FacilityLightSpec> lights;
    std::vector<FacilityCratePropSpec> crates;
    FacilityBlastDoorSpec blastDoor;

    // Capsule-CENTER spawn position (see core::CharacterController::spawn(),
    // which forwards this straight into Physics::createCharacterCapsule()
    // as the body's center) -- NOT feet-level, unlike aiSpawns above. Two
    // different position conventions on the same struct is exactly the
    // kind of silent unit mismatch that produced FPSPlayerController's own
    // center-vs-feet bug; this comment is the one place that fact needs to
    // be spelled out for whoever edits playerSpawn next.
    glm::vec3 playerSpawn{0.0f};
};

// Pure, headless, no ECS/Vulkan dependency -- independently unit-testable
// (engine/tests/test_main.cpp), same split housedemo::computeHouseLayout()
// established. Three rooms ("EntryHall", "RestrictedWing", "EscapeRoom")
// joined by two unlocked crossings: EntryHall holds the player's spawn
// point, a footlocker (LootKind::SanityInjector), a hiding-spot locker, and
// a standalone Red keycard. RestrictedWing holds the slower duffel bag
// (LootKind::Keycard, Gold) plus the Tormentor and dormant Culler; the Back
// Corridor between the two holds a third, standalone Blue keycard. All
// three keycards count toward the showcase's real gate: the blastDoor at
// RestrictedWing's own east wall gap into EscapeRoom, which
// EscapeGameLoop::tryUnlockExitWithObjective() only opens once the player's
// despair::ObjectiveManager has collected kKeycardsRequiredForExit (3) of
// them, any tier -- unlocking it is this vertical slice's real win
// condition. Footprint: EntryHall x[-3,3] z[-3,3], corridor x[3,7] z[-1,1],
// RestrictedWing x[7,13] z[-3,3], EscapeRoom x[13,17] z[-2,2], wall height
// 3m, centered at floor level (y=0) on facility-local origin.
//
// EscapeRoom's east wall (x=17), formerly a single solid "nothing beyond
// it" box, now has its own 2m gap (z[-1,1], unlocked -- no new keycard
// tier) into the "Deep Storage Wing": a real, larger showcase area built
// from computeDeepStorageWingGrid()'s ASCII grid rather than hand-authored
// per-wall boxes, adding a central hub, three branching rooms, and a real
// loop (hub -> east room -> a north-then-south corridor -> north room ->
// hub) back to the wing's own entrance -- see that function's own comment
// for the grid legend and exact layout.
[[nodiscard]] FacilityLayout computeFacilityLayout();

// The Deep Storage Wing's own raw layout data, pure and exposed
// separately from computeFacilityLayout() specifically so
// engine/tests/test_main.cpp can flood-fill it directly and assert every
// room is actually reachable from the entrance -- the one mechanical check
// available for "is this hand-authored ASCII grid actually connected the
// way its author intended," since nothing else in this pure/headless half
// of the engine can render or walk the level to find out. `rows[r][c]` is
// one of: '#' (a full 2m x 3m x 2m wall block, no floor slab beneath it),
// '.' (open floor, no wall), or '+' (open floor, plus a real unlocked
// core::Door leaf -- see computeFacilityLayout()'s appendGridWing() for
// how leaf orientation is picked from neighboring open cells). Cell (r, c)
// sits at facility-local (originX + c * cellSize, originZ + r * cellSize).
struct FacilityGridWing {
    std::vector<std::string> rows;
    float originX = 0.0f;
    float originZ = 0.0f;
    float cellSize = 2.0f;
};

// Legend and exact room/corridor placement:
//   Hub: rows[5..7][4..6]. North room: rows[1..3][4..6], gapped through the
//   hub at rows[4][5]. South room: rows[9..11][4..6], gapped at rows[8][5]
//   (a dead-end branch, not part of the loop). East room: rows[5..7][8..9],
//   gapped through the hub at rows[6][7]. The wing's own entrance is the
//   '+' door at rows[6][0] (aligned with EscapeRoom's own east gap, so it
//   sits directly across the 2m void this wing's originX leaves for it),
//   with an entrance corridor at rows[6][1..3] leading straight into the
//   hub. The loop: rows[1][7..9] runs east off the north room's own east
//   edge, then rows[2..4][9] turns south to meet the east room's own
//   top-left corner at rows[5][9] -- a real second route between the north
//   and east rooms that doesn't pass back through the hub, closing a loop
//   back to the entrance rather than leaving this wing a dead-end tree.
[[nodiscard]] FacilityGridWing computeDeepStorageWingGrid();

} // namespace engine::despair
