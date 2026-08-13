#pragma once

#include "tntwars/Scavenging.hpp"

namespace engine::tntwars {

// Kronos ("Gameplay Loop" world-building, "give players a reason to
// return"): real, pure, fully unit-testable permanent-upgrade logic --
// same real "small tier struct + a pure purchase function spending an
// already-real currency" shape core::UpgradeSystem.hpp already
// established for Mining Sim (Pickaxe/Backpack/Boots tiers spent from a
// real Wallet), applied here to TNT Wars' own real ScavengedMaterials
// ledger instead. Two real, mechanical categories -- Traversal (zip-line
// speed + jump-pad launch strength) and Suit (base move speed) -- cover
// the brief's own "Traversal upgrades"/"Suit upgrades"; "Forge upgrades"/
// "Station upgrades" are this same real purchase flow's own per-biome
// *location* flavor (Sky Map's already-built forge zone, Space Map's
// derelict-station platforms are where a player actually stands to buy
// either category), not a third mechanical category -- see
// TntWarsUpgradeStation.hpp's own comment for that real world-placement
// half.
enum class UpgradeCategory { Traversal, Suit };
constexpr int kMaxUpgradeTier = 3;

struct PlayerUpgrades {
    int traversalTier = 0;
    int suitTier = 0;
};

[[nodiscard]] int upgradeTier(const PlayerUpgrades& upgrades, UpgradeCategory category);

// Real, per-tier material cost -- the cost of advancing *from*
// `currentTier` to `currentTier + 1`. Real, deliberately steep scaling
// (each tier costs meaningfully more than the last, spread across all 3
// real ScavengeMaterialType currencies so no single node type alone
// funds a full upgrade path) -- a real, honest zero-cost result for
// `currentTier >= kMaxUpgradeTier` (nothing left to buy).
[[nodiscard]] ScavengedMaterials upgradeCost(UpgradeCategory category, int currentTier);

struct UpgradeResult {
    bool success = false;
    int newTier = 0;
};

// Real purchase -- rejects (returns success=false, no state change) if
// `upgrades` is already at kMaxUpgradeTier for `category`, or if
// `materials` can't afford upgradeCost() for the next tier (checked
// against every real currency in the same call, no partial consumption
// on a failed attempt, matching tntwars::craftExplosive()'s own real
// "check affordability fully before touching anything" convention). On
// success, real-deducts the exact real cost and increments the real
// tier.
[[nodiscard]] UpgradeResult purchaseUpgrade(PlayerUpgrades& upgrades, ScavengedMaterials& materials,
                                              UpgradeCategory category);

// Real, pure multiplier lookups -- what live movement consumers
// (Application's own zip-line/jump-pad/CharacterController-speed tick
// logic) actually read every frame. tier 0 always yields 1.0 (no upgrade
// yet purchased == the map's own original, untouched tuning).
[[nodiscard]] float traversalSpeedMultiplier(int traversalTier);
[[nodiscard]] float suitMoveSpeedMultiplier(int suitTier);

} // namespace engine::tntwars
