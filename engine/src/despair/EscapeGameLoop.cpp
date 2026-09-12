#include "despair/EscapeGameLoop.hpp"

namespace engine::despair {

bool toggleBreaker(PowerBreaker& breaker) {
    breaker.activated = !breaker.activated;
    return breaker.activated;
}

DoorUnlockResult tryEscapeThroughBlastDoor(LockedDoor& blastDoor, const KeycardInventory& inventory, const PowerBreaker& breaker) {
    if (!blastDoor.locked) return DoorUnlockResult::NotLocked;
    if (!hasKeycardTier(inventory, blastDoor.requiredTier)) return DoorUnlockResult::DeniedMissingKeycard;
    if (!breaker.activated) return DoorUnlockResult::DeniedMissingKeycard;

    blastDoor.locked = false;
    return DoorUnlockResult::Unlocked;
}

bool isPlayerCaught(glm::vec3 playerPos, glm::vec3 threatPos, float catchRadius) {
    return glm::distance(playerPos, threatPos) <= catchRadius;
}

void updateEscapeGameState(EscapeGameState& state, bool sanityDepleted, bool caughtByThreat, bool escapedThroughBlastDoor) {
    if (state.outcome != EscapeOutcome::InProgress) return;

    if (escapedThroughBlastDoor) {
        state.outcome = EscapeOutcome::Victory;
    } else if (caughtByThreat) {
        state.outcome = EscapeOutcome::CaughtByHunter;
    } else if (sanityDepleted) {
        state.outcome = EscapeOutcome::LostToMadness;
    }
}

} // namespace engine::despair
