#pragma once

#include <array>
#include <cstddef>

#include "core/Inventory.hpp"
#include "core/OreNode.hpp"
#include "miningsim/MiningTools.hpp"
#include "miningsim/Rarity.hpp"

namespace engine::miningsim {

// Kronos roadmap Milestone 16 ("Forging system"): real recipes that turn
// real, player-mined ore (core::Inventory/core::OreType -- the same real
// economy core::OreNode's own mine-on-swing loop already feeds, see
// core/Inventory.hpp) into one of Milestone 15's own real
// MiningToolType tools, real-gated by a rarity tier the player must have
// already unlocked (Milestone 13's own real RarityTier ladder). A real,
// deliberate sibling to tntwars::craftExplosive() (Milestone 6) -- same
// "real ingredient cost, real gate, real crafted result" shape -- but
// consuming core::Inventory directly rather than a parallel ledger,
// because real mined ore (unlike TNT-Wars' battlefield scrap) already
// has a real, existing inventory system to plug into.
struct ForgeRecipeCost {
    core::OreType oreType = core::OreType::Copper;
    int quantity = 0; // real-0 means "this ingredient slot is unused"
};
constexpr size_t kMaxForgeIngredients = 3;

struct ForgeRecipe {
    MiningToolType result = MiningToolType::Pickaxe;
    // Real minimum rarity tier the player must have already unlocked
    // (see isForgeRarityUnlocked() below) before this recipe is even
    // attemptable -- independent of whether they can afford its real
    // ore cost.
    RarityTier requiredRarity = RarityTier::Common;
    std::array<ForgeRecipeCost, kMaxForgeIngredients> costs{};
};
[[nodiscard]] const ForgeRecipe& forgeRecipeFor(MiningToolType tool);

[[nodiscard]] bool canAffordForgeRecipe(const core::Inventory& inventory, MiningToolType tool);

// Real, direct rank comparison against Milestone 13's own real
// rarityTierRank() -- a player who has unlocked Epic can also forge
// every real Common/Rare/Epic recipe, not just an exact-tier match.
[[nodiscard]] bool isForgeRarityUnlocked(RarityTier playerUnlockedRarity, MiningToolType tool);

struct ForgeResult {
    bool success = false;
    MiningToolType tool = MiningToolType::Pickaxe;
};

// Real forge attempt: fails (returns success=false, no inventory change
// at all) unless BOTH isForgeRarityUnlocked() and canAffordForgeRecipe()
// are real-true -- otherwise real-removes exactly the recipe's own ore
// cost via core::removeItem() and reports the real forged tool.
[[nodiscard]] ForgeResult forgeTool(core::Inventory& inventory, RarityTier playerUnlockedRarity, MiningToolType tool);

} // namespace engine::miningsim
