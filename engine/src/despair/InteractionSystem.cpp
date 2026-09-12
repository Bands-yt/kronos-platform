#include "despair/InteractionSystem.hpp"

#include <algorithm>

namespace engine::despair {

// ---------------------------------------------------------------------
// Raycast container searching / duffel-bag hold-to-search.

bool tickContainerSearchProgress(LootContainer& container, bool holding, float dt) {
    if (container.searched) return false;

    if (!holding) {
        container.searchProgressSeconds = 0.0f;
        return false;
    }

    container.searchProgressSeconds += dt;
    if (container.searchProgressSeconds < container.searchDurationSeconds) return false;

    container.searched = true;
    return true;
}

std::vector<core::EntityId> tickContainerSearches(float dt, core::ECS& ecs, bool interactDown,
                                                    core::EntityId lookAtTarget) {
    std::vector<core::EntityId> justSearched;

    auto view = ecs.view<LootContainer>();
    for (auto entity : view) {
        auto& container = view.get<LootContainer>(entity);
        bool holding = interactDown && entity == lookAtTarget;
        if (tickContainerSearchProgress(container, holding, dt)) {
            justSearched.push_back(entity);
        }
    }

    return justSearched;
}

// ---------------------------------------------------------------------
// Keycard inventory / door unlocking.

const char* keycardTierName(KeycardTier tier) {
    switch (tier) {
        case KeycardTier::Red: return "Red";
        case KeycardTier::Blue: return "Blue";
        case KeycardTier::Gold: return "Gold";
    }
    return "Unknown";
}

bool hasKeycardTier(const KeycardInventory& inventory, KeycardTier tier) {
    return std::find(inventory.heldTiers.begin(), inventory.heldTiers.end(), tier) != inventory.heldTiers.end();
}

bool addKeycardTier(KeycardInventory& inventory, KeycardTier tier) {
    if (hasKeycardTier(inventory, tier)) return false;
    inventory.heldTiers.push_back(tier);
    return true;
}

DoorUnlockResult tryUnlockDoor(LockedDoor& lockedDoor, const KeycardInventory& inventory) {
    if (!lockedDoor.locked) return DoorUnlockResult::NotLocked;
    if (!hasKeycardTier(inventory, lockedDoor.requiredTier)) return DoorUnlockResult::DeniedMissingKeycard;

    lockedDoor.locked = false;
    return DoorUnlockResult::Unlocked;
}

// ---------------------------------------------------------------------
// Hiding-spot entry/exit.

HidingTransition tryToggleHiding(HidingSpot& spot, core::EntityId spotEntity, PlayerHidingState& playerState,
                                  glm::vec3 playerCurrentPos) {
    if (playerState.currentSpot == spotEntity) {
        playerState.currentSpot = core::kNullEntity;
        spot.occupied = false;
        return HidingTransition::Exited;
    }

    if (playerState.currentSpot != core::kNullEntity) return HidingTransition::Denied;
    if (spot.occupied) return HidingTransition::Denied;

    spot.occupied = true;
    playerState.currentSpot = spotEntity;
    playerState.positionBeforeHiding = playerCurrentPos;
    return HidingTransition::Entered;
}

} // namespace engine::despair
