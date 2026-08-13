#include "miningsim/Mob.hpp"

#include <algorithm>

namespace engine::miningsim {

MobState spawnMobOfRarity(RarityTier rarity) {
    MobState mob;
    mob.rarity = rarity;
    float scale = 1.0f + static_cast<float>(rarityTierRank(rarity)) * 0.6f;
    mob.maxHealth = 50.0f * scale;
    mob.health = mob.maxHealth;
    mob.moveSpeed = 3.0f + static_cast<float>(rarityTierRank(rarity)) * 0.4f;
    mob.attackDamagePerSecond = 8.0f * scale;
    return mob;
}

void tickMobBehaviorState(MobState& mob, glm::vec3 mobPosition, glm::vec3 targetPosition) {
    float distance = glm::length(targetPosition - mobPosition);
    switch (mob.behavior) {
        case MobBehaviorState::Idle:
            if (distance <= mob.detectionRadius) mob.behavior = MobBehaviorState::Pursue;
            break;
        case MobBehaviorState::Pursue:
            if (distance <= mob.attackRadius) mob.behavior = MobBehaviorState::Attack;
            else if (distance > mob.detectionRadius) mob.behavior = MobBehaviorState::Idle;
            break;
        case MobBehaviorState::Attack:
            if (distance > mob.attackRadius) mob.behavior = MobBehaviorState::Pursue;
            break;
    }
}

glm::vec3 mobPursuitStep(glm::vec3 mobPosition, glm::vec3 targetPosition, float moveSpeed, float dt) {
    if (dt <= 0.0f) return mobPosition;
    glm::vec3 delta = targetPosition - mobPosition;
    float distance = glm::length(delta);
    if (distance < 1e-4f) return mobPosition;
    float step = moveSpeed * dt;
    if (step >= distance) return targetPosition;
    return mobPosition + (delta / distance) * step;
}

float mobAttackDamageThisTick(const MobState& mob, float dt) {
    if (mob.behavior != MobBehaviorState::Attack || dt <= 0.0f) return 0.0f;
    return mob.attackDamagePerSecond * dt;
}

void applyDamageToMob(MobState& mob, float damage) {
    if (damage <= 0.0f) return;
    mob.health = std::max(0.0f, mob.health - damage);
}

bool isMobAlive(const MobState& mob) { return mob.health > 0.0f; }

} // namespace engine::miningsim
