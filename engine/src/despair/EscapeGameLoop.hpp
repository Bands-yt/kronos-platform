#pragma once

#include <glm/glm.hpp>

#include "despair/InteractionSystem.hpp"
#include "despair/LootSystem.hpp"

namespace engine::despair {

// PROJECT: DESPAIR -- the vertical slice's own win/lose state machine.
// Layers on top of the same LockedDoor/tryUnlockDoor machinery every other
// door in this facility already uses, adding exactly the two things a
// generic door gate can't express on its own: a second real precondition
// (the facility breaker, not just a keycard tier) and a terminal outcome
// once that gate is actually cleared.

// A real, honest one-way latch, same "once summoned, stays summoned"
// precedent despair::DespairCullerAIState already establishes for this
// vertical slice -- once `outcome` leaves InProgress it never resets.
// Attached to the player entity by Application.cpp's own wiring, the same
// lazy-attach convention despair::PlayerHidingState uses.
enum class EscapeOutcome { InProgress, Victory, CaughtByHunter, LostToMadness };

struct EscapeGameState {
    EscapeOutcome outcome = EscapeOutcome::InProgress;
};

// The facility's power breaker -- the second half (alongside the Gold
// Master Keycard) of the blastDoor's own two-part gate. A real, honest
// toggle prop (core::Interactable + this component on the same entity,
// the same one-entity pairing convention LockedDoor/HidingSpot/Keycard all
// use), not a fabricated wire puzzle -- flipping it on is the entire
// mechanic.
struct PowerBreaker {
    bool activated = false;
};

// Pairs with a plain core::Light on the same entity so the facility's
// showcase wing rooms genuinely go from dark to lit when the player finds
// and flips the real breaker prop above, instead of PowerBreaker only ever
// recoloring itself (see toggleBreaker()'s own comment -- that part was
// already real). `litIntensity` is FacilityLightSpec::litIntensity carried
// over so Application.cpp's breaker-toggle handler has something to write
// back into Light::intensity without recomputing it; the Light itself
// starts at intensity 0 (spawned dark, since PowerBreaker::activated
// defaults false) and this component is the only thing that ever changes
// that.
struct BreakerPoweredLight {
    float litIntensity = 1.4f;
};

// Marks the one blastDoor entity FacilityMapBuilder.cpp spawns, so
// Application.cpp's interaction cascade can route it through
// tryEscapeThroughBlastDoor() instead of the generic tryUnlockDoor() path
// every other despair::LockedDoor uses -- both live on the same
// core::Door + despair::LockedDoor stack, so a tag is the only thing that
// distinguishes them at the target entity. Carries an unused field
// (rather than a truly empty struct) because EnTT specializes storage
// for empty component types to allocate nothing at all, and its
// emplace/emplace_or_replace return `void` for those -- which doesn't
// bind to ECS::addComponent()'s generic `Component&` return type.
struct BlastDoorTag {
    bool _unused = true;
};

// Pure toggle -- flips and returns the new `activated` state, the same
// "pure toggle, I/O-touching caller writes the Renderable" split
// core::toggleLamp() already establishes for a comparable world prop.
bool toggleBreaker(PowerBreaker& breaker);

// The facility's exit door -- layered on the exact same core::Door +
// core::Interactable + despair::LockedDoor stack every other door here
// uses (see LockedDoor's own header comment on why the leaf itself still
// carries no collider), but checked through this function instead of
// plain tryUnlockDoor(): NotLocked/Unlocked/DeniedMissingKeycard carry
// their usual tryUnlockDoor() meanings, except Unlocked additionally
// requires `breaker.activated` -- a correct Master Keycard with the power
// still off is reported as DeniedMissingKeycard too (from the player's
// perspective at the door, "the reader has no power" and "wrong card" are
// both just "this isn't opening"), so callers don't need a third outcome
// to distinguish them.
[[nodiscard]] DoorUnlockResult tryEscapeThroughBlastDoor(LockedDoor& blastDoor, const KeycardInventory& inventory,
                                                          const PowerBreaker& breaker);

// Simplified exit gate for this vertical slice's showcase objective loop:
// any kKeycardsRequiredForExit keycards (LootSystem.hpp's own
// ObjectiveManager, any tier) instead of a specific tier plus the
// facility breaker -- same NotLocked/Unlocked/DeniedMissingKeycard shape
// as tryUnlockDoor()/tryEscapeThroughBlastDoor() above, so callers need
// no new outcome to handle.
[[nodiscard]] DoorUnlockResult tryUnlockExitWithObjective(LockedDoor& blastDoor, const ObjectiveManager& objective);

// Real, honest proximity "caught" check. No attack/collision system
// exists anywhere in this engine for AI creatures -- they're plain
// Transform-driven with no collider at all (see HorrorAIManager.hpp's own
// header comment) -- so "caught" is defined the same straight-line,
// distance-based way every other AI perception check in this codebase
// already is, rather than inventing a fabricated hit-detection system.
// Pure -- the caller resolves which AI positions are actually threatening
// (e.g. only a Hunting Tormentor or an armed Culler, never a Dormant one)
// before calling this.
[[nodiscard]] bool isPlayerCaught(glm::vec3 playerPos, glm::vec3 threatPos, float catchRadius = 1.2f);

// Pure, single real writer for EscapeGameState -- called once per tick by
// Application.cpp's pre-tick hook with whatever that tick already
// resolved (SanityState::current <= 0, isPlayerCaught() against any live
// threat, or a real tryEscapeThroughBlastDoor() == Unlocked). A real,
// honest no-op once `state.outcome` has already left InProgress -- matches
// DespairCullerAIState's own one-way-latch precedent, not a
// re-triggerable state machine that could flip Victory back to
// LostToMadness on the same tick a slow caller keeps polling it.
void updateEscapeGameState(EscapeGameState& state, bool sanityDepleted, bool caughtByThreat, bool escapedThroughBlastDoor);

} // namespace engine::despair
