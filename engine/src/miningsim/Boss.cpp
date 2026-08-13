#include "miningsim/Boss.hpp"

#include <cmath>

namespace engine::miningsim {

MobState spawnBossOfRarity(RarityTier rarity) {
    MobState boss = spawnMobOfRarity(rarity);
    boss.maxHealth *= kBossStatMultiplier;
    boss.health = boss.maxHealth;
    boss.attackDamagePerSecond *= kBossStatMultiplier;
    boss.detectionRadius *= kBossDetectionRadiusScale;
    return boss;
}

float bossSpawnChance(RarityTier rarity) {
    return kBaseBossSpawnChance / std::pow(2.0f, static_cast<float>(rarityTierRank(rarity)));
}

bool rollBossSpawn(RarityTier rarity, std::mt19937& rng) {
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    return dist(rng) < bossSpawnChance(rarity);
}

float bossRewardMultiplier(RarityTier rarity) {
    return 1.0f + static_cast<float>(rarityTierRank(rarity)) * 1.5f;
}

} // namespace engine::miningsim
