#include "despair/LootSystem.hpp"

namespace engine::despair {

std::string grantContainerLoot(core::ECS& ecs, core::EntityId container, core::EntityId player, SanitySystem& sanitySystem) {
    auto* drop = ecs.tryGetComponent<LootDrop>(container);
    if (drop == nullptr || drop->kind == LootKind::None) return "";

    switch (drop->kind) {
        case LootKind::None:
            return "";
        case LootKind::Keycard: {
            auto* inventory = ecs.tryGetComponent<KeycardInventory>(player);
            if (inventory == nullptr) inventory = &ecs.addComponent<KeycardInventory>(player);
            // A duplicate-tier find (a second container yielding a tier
            // already held) is still a real, honest search outcome -- just
            // report it plainly rather than claiming a fresh pickup, same
            // "no lying about what happened" spirit addKeycardTier()'s own
            // comment documents for its no-op case.
            bool wasNew = addKeycardTier(*inventory, drop->keycardTier);
            std::string tierName = keycardTierName(drop->keycardTier);
            return wasNew ? "Found a " + tierName + " keycard." : "Found a " + tierName + " keycard (already held).";
        }
        case LootKind::SanityInjector: {
            sanitySystem.applyInstantDelta(ecs, player, drop->sanityRestoreAmount);
            return "Found an Anti-Psychotic Injector. Sanity restored.";
        }
    }
    return "";
}

} // namespace engine::despair
