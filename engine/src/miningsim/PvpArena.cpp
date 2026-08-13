#include "miningsim/PvpArena.hpp"

#include <algorithm>

namespace engine::miningsim {

void applyToolDamageToPlayer(PvpPlayerState& target, MiningToolType tool) {
    float damage = static_cast<float>(miningToolStatsFor(tool).miningPower) * kPvpToolDamageScale;
    target.health = std::max(0.0f, target.health - damage);
}

bool isPvpPlayerAlive(const PvpPlayerState& player) { return player.health > 0.0f; }

float pvpRewardMultiplier(RarityTier arenaRarity) {
    return 1.0f + static_cast<float>(rarityTierRank(arenaRarity)) * 1.2f;
}

} // namespace engine::miningsim
