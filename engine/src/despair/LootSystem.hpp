#pragma once

#include <array>
#include <string>

#include "core/ECS.hpp"
#include "despair/InteractionSystem.hpp"
#include "despair/SanitySystem.hpp"

namespace engine::despair {

// PROJECT: DESPAIR -- what a LootContainer actually contains, reconciling
// the "held it down long enough" mechanical core InteractionSystem.hpp's
// own LootContainer already provides with the real, honest granting step
// its own header comment explicitly deferred: "What happens once a
// container is searched (spawn an item, play a sound) is LootSystem's
// job, hooked off the completion this mechanic reports." A duffel bag, a
// footlocker, and a body are all still just LootContainer -- this adds
// only the "what's inside," not a parallel container type.
enum class LootKind { None, Keycard, SanityInjector };

// Attached alongside LootContainer on the same entity -- same one-entity
// pairing convention despair::Keycard/core::Pickup already establishes
// (InteractionSystem.hpp), so LootContainer itself never needs to know or
// care what granting loot even means.
struct LootDrop {
    LootKind kind = LootKind::None;

    // Only meaningful when kind == Keycard.
    KeycardTier keycardTier = KeycardTier::Red;

    // Only meaningful when kind == SanityInjector -- the Anti-Psychotic
    // Injector's own +50 (see SanitySystem.hpp's SanityState header
    // comment), applied through SanitySystem::applyInstantDelta(), the
    // one real writer that system exposes for exactly this kind of
    // one-off relief.
    float sanityRestoreAmount = 50.0f;
};

// Real hotbar -- what a search actually hands the player something for,
// instead of grantContainerLoot() applying/discarding it on the spot.
// Keycards still gate doors through KeycardInventory exactly as before
// (see Application.cpp's own door-unlock cascade); a HotbarSlot entry for
// a found keycard is purely a real, honest display of what
// KeycardInventory already holds. Anti-Psychotic Injectors are the one
// kind this hotbar is the real source of truth for -- grantContainerLoot()
// stores them here instead of auto-applying on pickup, and useHotbarSlot()
// below is the only place that ever spends one.
constexpr int kHotbarSlotCount = 4;

struct HotbarSlot {
    LootKind kind = LootKind::None;
    KeycardTier keycardTier = KeycardTier::Red; // only meaningful when kind == Keycard
    int count = 0;
};

struct HotbarInventory {
    std::array<HotbarSlot, kHotbarSlotCount> slots{};
};

// Adds one unit of `kind` (Keycard also carries `tier`) to the first
// matching or empty slot -- a real, honest stack, not a fabricated
// unlimited pickup. Returns false (no-op) if every slot is already full
// with a different kind.
bool addHotbarItem(HotbarInventory& hotbar, LootKind kind, KeycardTier tier = KeycardTier::Red);

// Spends one unit of the item in `slotIndex`: for SanityInjector, applies
// its real sanity restore through SanitySystem::applyInstantDelta() and
// decrements the stack (clearing the slot at 0); any other kind (or an
// empty slot) is a real, honest no-op -- a keycard is already spent
// automatically at the door it matches, not something this hotbar "uses".
// Returns a short description for the caller's own floating-text use, or
// an empty string for the no-op case.
[[nodiscard]] std::string useHotbarSlot(core::ECS& ecs, HotbarInventory& hotbar, int slotIndex, core::EntityId player,
                                         SanitySystem& sanitySystem);

// Real granting step, called once per entity id `tickContainerSearches()`
// (InteractionSystem.hpp) returns as just-finished this tick (see
// Application.cpp's own wiring, right where that vector is already
// scanned). `container` is the just-searched LootContainer/LootDrop
// entity; `player` receives the loot. A container with no LootDrop
// component, or LootKind::None, is a real, honest no-op -- "searched it,
// found nothing" is a valid, common outcome here, not an error -- and
// returns an empty string. Otherwise returns a short, human-readable
// description of what was granted (e.g. "Found a Red keycard.", "Found an
// Anti-Psychotic Injector."), for the caller's own floating-text/event use
// -- this function does no I/O itself, matching core::toggleDoor()'s own
// "pure decision + write, caller handles presentation" split.
[[nodiscard]] std::string grantContainerLoot(core::ECS& ecs, core::EntityId container, core::EntityId player,
                                              SanitySystem& sanitySystem);

// Real, honest objective counter for the simplified "find N keycards,
// reach the exit" loop EscapeGameLoop.hpp's tryUnlockExitWithObjective()
// gates on -- any tier counts, unlike KeycardInventory's own per-tier
// set (which still separately gates any plain despair::LockedDoor a
// specific tier is required for). Lazily attached to the player the
// first time a keycard is actually granted, same convention as
// HotbarInventory above.
constexpr int kKeycardsRequiredForExit = 3;

struct ObjectiveManager {
    int keycardsCollected = 0;
};

// Increments keycardsCollected by one -- called once per real, newly
// granted keycard (see grantContainerLoot()'s own call site and
// Application.cpp's direct despair::Keycard pickup branch), never for a
// duplicate-tier find that addKeycardTier() already reports as a no-op.
void addKeycardToObjective(core::ECS& ecs, core::EntityId player);

} // namespace engine::despair
