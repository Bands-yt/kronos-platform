#include "tntwars/TntWarsUpgrades.hpp"

namespace engine::tntwars {

namespace {
// Real, per-category, per-tier cost table -- index 0 is the cost of
// tier 0->1, index 1 of tier 1->2, index 2 of tier 2->3 (kMaxUpgradeTier
// entries per category). Deliberately steep, spread across all 3 real
// currencies -- see this file's own header comment.
constexpr ScavengedMaterials kTraversalCosts[kMaxUpgradeTier] = {
    {{20, 10, 5}},
    {{40, 25, 15}},
    {{70, 45, 30}},
};
constexpr ScavengedMaterials kSuitCosts[kMaxUpgradeTier] = {
    {{10, 20, 10}},
    {{20, 40, 25}},
    {{35, 65, 45}},
};
} // namespace

int upgradeTier(const PlayerUpgrades& upgrades, UpgradeCategory category) {
    return category == UpgradeCategory::Traversal ? upgrades.traversalTier : upgrades.suitTier;
}

ScavengedMaterials upgradeCost(UpgradeCategory category, int currentTier) {
    if (currentTier < 0 || currentTier >= kMaxUpgradeTier) return ScavengedMaterials{};
    return category == UpgradeCategory::Traversal ? kTraversalCosts[currentTier] : kSuitCosts[currentTier];
}

UpgradeResult purchaseUpgrade(PlayerUpgrades& upgrades, ScavengedMaterials& materials, UpgradeCategory category) {
    UpgradeResult result;
    int currentTier = upgradeTier(upgrades, category);
    if (currentTier >= kMaxUpgradeTier) return result;

    ScavengedMaterials cost = upgradeCost(category, currentTier);
    for (size_t i = 0; i < kScavengeMaterialTypeCount; ++i) {
        if (materials.counts[i] < cost.counts[i]) return result; // real, honest "can't afford" -- no partial spend
    }
    for (size_t i = 0; i < kScavengeMaterialTypeCount; ++i) {
        materials.counts[i] -= cost.counts[i];
    }

    int newTier = currentTier + 1;
    if (category == UpgradeCategory::Traversal) {
        upgrades.traversalTier = newTier;
    } else {
        upgrades.suitTier = newTier;
    }
    result.success = true;
    result.newTier = newTier;
    return result;
}

float traversalSpeedMultiplier(int traversalTier) { return 1.0f + 0.15f * static_cast<float>(traversalTier); }

float suitMoveSpeedMultiplier(int suitTier) { return 1.0f + 0.12f * static_cast<float>(suitTier); }

} // namespace engine::tntwars
