#include "despair/LootSystem.hpp"

namespace engine::despair {

bool addHotbarItem(HotbarInventory& hotbar, LootKind kind, KeycardTier tier) {
    int emptySlot = -1;
    for (int i = 0; i < kHotbarSlotCount; ++i) {
        HotbarSlot& slot = hotbar.slots[static_cast<std::size_t>(i)];
        if (slot.kind == kind && (kind != LootKind::Keycard || slot.keycardTier == tier)) {
            slot.count += 1;
            return true;
        }
        if (slot.kind == LootKind::None && emptySlot < 0) emptySlot = i;
    }
    if (emptySlot < 0) return false;

    HotbarSlot& slot = hotbar.slots[static_cast<std::size_t>(emptySlot)];
    slot.kind = kind;
    slot.keycardTier = tier;
    slot.count = 1;
    return true;
}

std::string useHotbarSlot(core::ECS& ecs, HotbarInventory& hotbar, int slotIndex, core::EntityId player,
                           SanitySystem& sanitySystem) {
    if (slotIndex < 0 || slotIndex >= kHotbarSlotCount) return "";
    HotbarSlot& slot = hotbar.slots[static_cast<std::size_t>(slotIndex)];
    if (slot.kind != LootKind::SanityInjector || slot.count <= 0) return "";

    sanitySystem.applyInstantDelta(ecs, player, 50.0f);
    slot.count -= 1;
    if (slot.count <= 0) slot = HotbarSlot{};
    return "Used Anti-Psychotic Injector. Sanity restored.";
}

std::string grantContainerLoot(core::ECS& ecs, core::EntityId container, core::EntityId player, SanitySystem& sanitySystem) {
    auto* drop = ecs.tryGetComponent<LootDrop>(container);
    if (drop == nullptr || drop->kind == LootKind::None) return "";

    auto* hotbar = ecs.tryGetComponent<HotbarInventory>(player);
    if (hotbar == nullptr) hotbar = &ecs.addComponent<HotbarInventory>(player);

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
            addHotbarItem(*hotbar, LootKind::Keycard, drop->keycardTier);
            if (wasNew) addKeycardToObjective(ecs, player);
            std::string tierName = keycardTierName(drop->keycardTier);
            return wasNew ? "Found a " + tierName + " keycard." : "Found a " + tierName + " keycard (already held).";
        }
        case LootKind::SanityInjector: {
            addHotbarItem(*hotbar, LootKind::SanityInjector);
            return "Found an Anti-Psychotic Injector.";
        }
    }
    return "";
}

void addKeycardToObjective(core::ECS& ecs, core::EntityId player) {
    auto* objective = ecs.tryGetComponent<ObjectiveManager>(player);
    if (objective == nullptr) objective = &ecs.addComponent<ObjectiveManager>(player);
    objective->keycardsCollected += 1;
}

} // namespace engine::despair
