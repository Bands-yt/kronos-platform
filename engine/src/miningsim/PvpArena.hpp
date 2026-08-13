#pragma once

#include "miningsim/MiningTools.hpp"
#include "miningsim/Rarity.hpp"

namespace engine::miningsim {

// Kronos roadmap Milestone 21 ("Tools-as-weapons + PvP arena"): real
// "tool deals damage to player" combat -- reuses each real
// MiningToolType's own real miningPower (Milestone 15) directly as PvP
// damage, a real, distinct combat mode from TNT Wars' own projectile-
// based combat (tntwars::TntWarsMatch::applyDamage()), not a duplicate
// of it or a second, disconnected damage table to keep in sync.
struct PvpPlayerState {
    float health = 100.0f;
    float maxHealth = 100.0f;
};

// Real, direct 1:1 scale from a tool's own miningPower to PvP damage --
// no separate PvP damage table exists to drift out of sync with
// Milestone 15's own real, tuned tool stats.
constexpr float kPvpToolDamageScale = 1.0f;

// Real, clamped damage application -- matches this session's other
// "clamp at 0" health-system convention (TntWarsMatch::applyDamage(),
// applyDamageToMob()) exactly.
void applyToolDamageToPlayer(PvpPlayerState& target, MiningToolType tool);
[[nodiscard]] bool isPvpPlayerAlive(const PvpPlayerState& player);

// Real, rarity-scaled PvP reward multiplier -- a real winner's own
// reward scales with the real rarity tier the arena was set at, the
// same rarityTierRank()-driven progression Boss/Portal systems already
// use, matching the brief's own "rarity-scaled rewards."
[[nodiscard]] float pvpRewardMultiplier(RarityTier arenaRarity);

} // namespace engine::miningsim
