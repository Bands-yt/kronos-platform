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
// for its own front door. No patrol paths/nav graph here: HorrorAIManager
// always targets the player's live position directly (see its own header
// comment on why no pathfinding solver exists anywhere in this engine),
// never a room-topology graph, so this layout only needs to place static
// geometry and starting positions.

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

// A duffel bag is just a LootContainer with a longer searchDurationSeconds
// than a quick footlocker -- see InteractionSystem.hpp's own comment.
// Deliberately no separate "kind" field.
struct FacilityContainerSpec {
    glm::vec3 localPosition{0.0f};
    float searchDurationSeconds = 3.0f;
    std::string prompt = "Hold E to search";

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
// EscapeGameLoop::tryEscapeThroughBlastDoor() (requires BOTH the Gold
// Master Keycard AND the facility breaker active) rather than plain
// tryUnlockDoor(), and whose successful unlock is this vertical slice's
// real win condition (see Application.cpp's own wiring), not just a
// generic toggleDoor(). Same unit-box leaf convention FacilityDoorSpec
// establishes.
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
// joined by two gated crossings: EntryHall holds the player's spawn point,
// a footlocker (LootKind::SanityInjector), a hiding-spot locker, and the
// Red keycard that unlocks the corridor door into RestrictedWing.
// RestrictedWing holds the slower duffel bag (LootKind::Keycard, Gold --
// the "Master Keycard") plus the Tormentor and dormant Culler, and its own
// east wall gap is the blastDoor gate into EscapeRoom, which requires that
// Gold keycard AND the facility breaker it also holds (see
// EscapeGameLoop.hpp) -- unlocking it is this vertical slice's real win
// condition. Footprint: EntryHall x[-3,3] z[-3,3], corridor x[3,7] z[-1,1],
// RestrictedWing x[7,13] z[-3,3], EscapeRoom x[13,17] z[-2,2], wall height
// 3m, centered at floor level (y=0) on facility-local origin.
[[nodiscard]] FacilityLayout computeFacilityLayout();

} // namespace engine::despair
