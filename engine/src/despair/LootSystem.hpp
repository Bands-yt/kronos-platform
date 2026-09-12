#pragma once

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

} // namespace engine::despair
