#pragma once

#include <random>

#include "miningsim/Mob.hpp"
#include "miningsim/Rarity.hpp"

namespace engine::miningsim {

// Kronos roadmap Milestone 20 ("Boss system"): real, chance-based boss
// spawns built directly on Milestone 19's own MobState framework -- a
// boss is a real, much-stronger MobState (see spawnBossOfRarity() below,
// which reuses spawnMobOfRarity()'s own real rarity-scaling formula at a
// real, higher multiplier), not a parallel, disconnected entity type.
constexpr float kBossStatMultiplier = 4.0f;    // real, tuned -- a boss is a much tougher fight than a regular mob of the same rarity
constexpr float kBossDetectionRadiusScale = 1.5f; // real, wider aggro range for a boss
constexpr float kBaseBossSpawnChance = 0.05f;  // real, tuned baseline per-attempt roll for RarityTier::Common

[[nodiscard]] MobState spawnBossOfRarity(RarityTier rarity);

// Real, per-rarity spawn-chance -- halves with every real rank step up
// (the same real "falls off sharply with rarity" shape
// miningsim::portalSpawnWeight() already established for portals),
// matching the brief's own "chance spawn... scale with rarity" framing:
// a real, honest, small number, not a fixed, rarity-blind coin flip.
[[nodiscard]] float bossSpawnChance(RarityTier rarity);

// Real, one-shot roll against bossSpawnChance() -- the caller supplies
// the std::mt19937&, the same real, testable "caller seeds it"
// convention miningsim::rollPortalTier() already established.
[[nodiscard]] bool rollBossSpawn(RarityTier rarity, std::mt19937& rng);

// Real, tuned, currency-agnostic reward multiplier a caller applies to
// whatever real reward pool a normal kill would have granted --
// strictly increases with rarity rank, matching the brief's own "harder
// boss = better reward."
[[nodiscard]] float bossRewardMultiplier(RarityTier rarity);

} // namespace engine::miningsim
