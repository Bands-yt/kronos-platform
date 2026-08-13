#include "core/UpgradeSystem.hpp"

#include <array>

namespace engine::core {

namespace {
// Real, tuned tier tables (task category 5 + task category 8's
// "Balancing"). Costs are geometric-ish across tiers (each roughly
// 3x the previous) so each purchase feels like a real milestone rather
// than a rounding error, paced against sellPriceForQuantity()'s real
// payouts (Economy.cpp) -- e.g. an Iron pickaxe (150 coins) is affordable
// after selling roughly one full stack of Copper ore at full price
// (10 units x 5 coins = 50, so ~3 stacks), a deliberate early-game pace.
constexpr std::array<UpgradeTierInfo, kPickaxeTierCount> kPickaxeTiers = {{
    {"Copper Pickaxe", 0, 1, 0, 0.0f, 0.0f},
    {"Iron Pickaxe", 150, 2, 0, 0.0f, 0.0f},
    {"Silver Pickaxe", 500, 3, 0, 0.0f, 0.0f},
    {"Gold Pickaxe", 1500, 5, 0, 0.0f, 0.0f},
    {"Platinum Pickaxe", 5000, 8, 0, 0.0f, 0.0f},
}};

constexpr std::array<UpgradeTierInfo, kBackpackTierCount> kBackpackTiers = {{
    {"Satchel", 0, 0, 12, 60.0f, 0.0f},
    {"Pack", 200, 0, 18, 90.0f, 0.0f},
    {"Rucksack", 700, 0, 26, 130.0f, 0.0f},
    {"Cargo Hold", 2000, 0, 36, 180.0f, 0.0f},
    {"Vault Pack", 6000, 0, 50, 250.0f, 0.0f},
}};

constexpr std::array<UpgradeTierInfo, kBootsTierCount> kBootsTiers = {{
    {"Worn Boots", 0, 0, 0, 0.0f, 1.00f},
    {"Sturdy Boots", 100, 0, 0, 0.0f, 1.10f},
    {"Reinforced Boots", 400, 0, 0, 0.0f, 1.20f},
    {"Swift Boots", 1200, 0, 0, 0.0f, 1.35f},
    {"Prospector's Boots", 4000, 0, 0, 0.0f, 1.50f},
}};
} // namespace

const UpgradeTierInfo& pickaxeTier(int tierIndex) { return kPickaxeTiers[static_cast<size_t>(tierIndex)]; }
const UpgradeTierInfo& backpackTier(int tierIndex) { return kBackpackTiers[static_cast<size_t>(tierIndex)]; }
const UpgradeTierInfo& bootsTier(int tierIndex) { return kBootsTiers[static_cast<size_t>(tierIndex)]; }

int tierCountFor(UpgradeCategory category) {
    switch (category) {
        case UpgradeCategory::Pickaxe: return kPickaxeTierCount;
        case UpgradeCategory::Backpack: return kBackpackTierCount;
        case UpgradeCategory::Boots: return kBootsTierCount;
    }
    return 0;
}

const UpgradeTierInfo& tierInfoFor(UpgradeCategory category, int tierIndex) {
    switch (category) {
        case UpgradeCategory::Pickaxe: return pickaxeTier(tierIndex);
        case UpgradeCategory::Backpack: return backpackTier(tierIndex);
        case UpgradeCategory::Boots: return bootsTier(tierIndex);
    }
    return pickaxeTier(0);
}

int miningPowerFor(const PlayerUpgrades& upgrades) { return pickaxeTier(upgrades.pickaxeTier).miningPower; }

float speedMultiplierFor(const PlayerUpgrades& upgrades) { return bootsTier(upgrades.bootsTier).speedMultiplier; }

void applyBackpackTier(Inventory& inventory, const PlayerUpgrades& upgrades) {
    const UpgradeTierInfo& tier = backpackTier(upgrades.backpackTier);
    inventory.slotCapacity = tier.slotCapacity;
    inventory.weightLimit = tier.weightLimit;
}

UpgradePurchaseResult purchaseUpgrade(PlayerUpgrades& upgrades, Wallet& wallet, UpgradeCategory category) {
    UpgradePurchaseResult result;

    int* currentTierIndex = nullptr;
    switch (category) {
        case UpgradeCategory::Pickaxe: currentTierIndex = &upgrades.pickaxeTier; break;
        case UpgradeCategory::Backpack: currentTierIndex = &upgrades.backpackTier; break;
        case UpgradeCategory::Boots: currentTierIndex = &upgrades.bootsTier; break;
    }

    int maxTier = tierCountFor(category) - 1;
    if (*currentTierIndex >= maxTier) {
        result.reason = "Already at max tier";
        return result;
    }

    int nextTierIndex = *currentTierIndex + 1;
    const UpgradeTierInfo& nextTier = tierInfoFor(category, nextTierIndex);
    if (wallet.coins < nextTier.cost) {
        result.reason = "Not enough coins";
        return result;
    }

    wallet.coins -= nextTier.cost;
    *currentTierIndex = nextTierIndex;

    result.success = true;
    result.coinsSpent = nextTier.cost;
    return result;
}

} // namespace engine::core
