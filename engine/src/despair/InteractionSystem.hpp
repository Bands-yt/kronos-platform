#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "core/ECS.hpp"

namespace engine::despair {

// PROJECT: DESPAIR -- the five interaction mechanics the vertical slice
// asks for (raycast container searching, duffel-bag hold-to-search,
// keycard inventory, door unlocking, hiding-spot entry/exit), built the
// same way Sprint 6/10's world-prop interactions already are: new
// component types plugged into Application.cpp's existing
// core::resolveInteractionTarget()/events.onInteract cascade (see
// core::Door/core::Pickup in core/Interactable.hpp), not a parallel
// interaction pipeline. Every ECS-touching function here is deliberately
// free-standing (no InteractionSystem "class"/update() of its own) --
// each mechanic below is either a direct reaction to a single resolved
// interact target (identical shape to core::toggleDoor()/collectPickup())
// or, for the one mechanic that genuinely needs a per-tick scan (hold-to-
// search), a single small free function taking exactly the ECS + the
// two already-resolved-per-tick values (`interactDown`, `lookAtTarget`)
// Application.cpp's own pre-tick hook already computes -- no new stored
// "current hold target" state anywhere, since a container's own
// searchProgressSeconds already resets itself the instant it isn't the
// one thing being looked at and held.

// ---------------------------------------------------------------------
// Raycast container searching / duffel-bag hold-to-search.
//
// A duffel bag is just a LootContainer with a longer searchDurationSeconds
// than a quick footlocker -- same component, same mechanic, no separate
// "duffel bag" type. Deliberately doesn't roll or grant any actual loot
// itself -- this is only the real, honest "held it down long enough,
// looking the whole time" mechanical core, matching core::Pickup's own
// stated scope boundary. What happens once a container is searched
// (granting a keycard, an Anti-Psychotic Injector's sanity restore) is
// despair::LootSystem's job (LootSystem.hpp's own LootDrop component),
// hooked off the completion this mechanic reports.
struct LootContainer {
    bool searched = false;
    float searchDurationSeconds = 3.0f;

    // Real progress state, not configuration -- resets to 0 the instant
    // `holding` goes false (see tickContainerSearchProgress()), so
    // looking away or releasing Interact mid-search throws the attempt
    // away rather than letting a player bank partial progress across
    // interrupted presses.
    float searchProgressSeconds = 0.0f;
};

// Pure. `holding` is the caller's own already-resolved "is Interact down
// AND is this exact entity what's being looked at right now" check (see
// tickContainerSearches() below) -- this function has no opinion on how
// that's decided. Returns true on the exact tick searchProgressSeconds
// crosses searchDurationSeconds (and sets `container.searched = true`);
// false every other tick, including every tick after the first true (a
// searched container never re-fires). Real, honest no-op if already
// searched.
[[nodiscard]] bool tickContainerSearchProgress(LootContainer& container, bool holding, float dt);

// ECS-only driver for the above, called once per tick from Application.cpp's
// pre-tick hook -- deliberately BEFORE the rising-edge "Interact just
// pressed" gate, not inside it: a hold-to-search mechanic needs to see
// Interact held continuously across many ticks, which an edge-triggered
// dispatch (one call per press) can never observe. Scans every
// LootContainer in the scene (the same "one small view scan per tick"
// shape HorrorAIManager's own tickStalkers()/tickTormentors() already
// use) so a container the player just looked away from gets its progress
// reset back to 0 on the very same tick, not left stale until next
// looked at. Returns every container that finished searching this tick,
// for the caller to fire events.onInteract on (see core::Pickup's own
// "mechanical core only, script/LootSystem layers the reaction" split).
[[nodiscard]] std::vector<core::EntityId> tickContainerSearches(float dt, core::ECS& ecs, bool interactDown,
                                                                  core::EntityId lookAtTarget);

// ---------------------------------------------------------------------
// Keycard inventory / door unlocking.

enum class KeycardTier { Red, Blue, Gold };

[[nodiscard]] const char* keycardTierName(KeycardTier tier);

// Attached to a real, pickup-able keycard entity in the world -- pairs
// with core::Pickup on the same entity so collectPickup() still handles
// hiding the Renderable/removing Interactable exactly like every other
// pickup; this component only carries the one fact core::Pickup doesn't:
// which tier this specific keycard is.
struct Keycard {
    KeycardTier tier = KeycardTier::Red;
};

// Attached to the player/character entity. Set semantics deliberately
// (see addKeycardTier() below), not a single "highest tier held" scalar
// -- a real facility plausibly has independently-gated doors, not one
// strictly-ordered progression, and a set costs nothing extra to check.
struct KeycardInventory {
    std::vector<KeycardTier> heldTiers;
};

[[nodiscard]] bool hasKeycardTier(const KeycardInventory& inventory, KeycardTier tier);

// Pure. Returns true if `tier` was newly added, false if already held --
// real set semantics, so interacting with a second copy of a tier the
// player already has (or re-resolving the same already-collected keycard
// -- see resolveInteractionTarget()'s own documented permissive fallback
// for entities with no Interactable component) is a harmless no-op, not
// a duplicate entry.
bool addKeycardTier(KeycardInventory& inventory, KeycardTier tier);

enum class DoorUnlockResult { NotLocked, Unlocked, DeniedMissingKeycard };

// Attached alongside core::Door + core::Transform on a door entity that
// should start locked -- layers a keycard gate on top of the existing
// generic open/close mechanic rather than replacing it (see
// core::Door's own header comment: still just a direct rotation write
// between two authored orientations, no physics hinge). Once unlocked,
// stays unlocked for the rest of the level -- a real facility keycard
// reader, not a re-lockable puzzle mechanic.
struct LockedDoor {
    KeycardTier requiredTier = KeycardTier::Red;
    bool locked = true;
};

// Pure. NotLocked if the door was already unlocked (caller should still
// go ahead and toggleDoor() as normal); Unlocked the first time the
// player's inventory actually has the required tier (also flips
// `lockedDoor.locked` false, permanently, and the caller should
// toggleDoor() this same press too -- a real "swipe card, door opens,"
// not two separate presses); DeniedMissingKeycard otherwise, in which
// case the caller must NOT call toggleDoor() -- the whole point of the
// gate.
[[nodiscard]] DoorUnlockResult tryUnlockDoor(LockedDoor& lockedDoor, const KeycardInventory& inventory);

// ---------------------------------------------------------------------
// Hiding-spot entry/exit.

// A real, walk-in hiding spot (a locker, under a bed) -- `occupied`
// keeps two players (or a lone player re-triggering the same spot from
// two different lookAtTarget/proximity resolutions in one tick, though
// that can't currently happen) from stacking into the same spot.
struct HidingSpot {
    bool occupied = false;
    glm::vec3 interiorPosition{0.0f};
};

// Attached to the player/character entity -- kNullEntity (the default)
// means "not currently hiding." Lazily added the first time the player
// ever interacts with any HidingSpot (see Application.cpp's wiring), not
// required at scene-build time.
struct PlayerHidingState {
    core::EntityId currentSpot = core::kNullEntity;

    // Snapshotted on entry, restored on exit -- the same "come back out
    // where you actually were, not some fixed spawn point" property
    // core::TeleportPad's own destination-only design deliberately
    // doesn't need (a teleport has no "return trip").
    glm::vec3 positionBeforeHiding{0.0f};
};

enum class HidingTransition { Entered, Exited, Denied };

// Pure (aside from needing the two entities' own component references
// and ids passed in -- no live ECS/Physics lookups of its own, matching
// core::toggleDoor()'s "pure decision + write" shape). `spotEntity` is
// `spot`'s own id, needed only to compare against/store into
// `playerState.currentSpot` (a pure function can't discover its own
// caller's entity id any other way).
//
// - Already hiding in exactly this spot: exits (Exited) -- an Interact
//   press while hidden always means "get out," regardless of what the
//   raycast happens to resolve to from inside a locker.
// - Not hiding anywhere, spot is free: enters (Entered), snapshotting
//   `playerCurrentPos` into positionBeforeHiding.
// - Spot already occupied by someone else, or player is already hiding
//   in a *different* spot: Denied, a real, honest no-op.
[[nodiscard]] HidingTransition tryToggleHiding(HidingSpot& spot, core::EntityId spotEntity,
                                                PlayerHidingState& playerState, glm::vec3 playerCurrentPos);

} // namespace engine::despair
