#include "miningsim/Forging.hpp"

namespace engine::miningsim {

namespace {
constexpr ForgeRecipe kForgeRecipes[] = {
    {MiningToolType::Pickaxe, RarityTier::Common, {{{core::OreType::Copper, 5}, {}, {}}}},
    {MiningToolType::Drill, RarityTier::Common, {{{core::OreType::Copper, 8}, {core::OreType::Iron, 4}, {}}}},
    {MiningToolType::Laser, RarityTier::Rare, {{{core::OreType::Iron, 6}, {core::OreType::Silver, 3}, {}}}},
    {MiningToolType::PlasmaCutter, RarityTier::Rare, {{{core::OreType::Silver, 6}, {core::OreType::Gold, 2}, {}}}},
    {MiningToolType::SonicResonator, RarityTier::Epic, {{{core::OreType::Gold, 5}, {core::OreType::Platinum, 2}, {}}}},
    {MiningToolType::GravityHammer, RarityTier::Epic, {{{core::OreType::Platinum, 6}, {core::OreType::Iron, 10}, {}}}},
    {MiningToolType::ExplosiveCharge, RarityTier::Legendary, {{{core::OreType::Platinum, 8}, {core::OreType::Crystal, 3}, {}}}},
    {MiningToolType::HydroDrill, RarityTier::Rare, {{{core::OreType::Silver, 5}, {core::OreType::Iron, 5}, {}}}},
    {MiningToolType::VoidExtractor, RarityTier::UltraLegendary, {{{core::OreType::Crystal, 10}, {}, {}}}},
    {MiningToolType::HeavenlyChisel, RarityTier::Legendary, {{{core::OreType::Crystal, 6}, {core::OreType::Gold, 4}, {}}}},
};
} // namespace

const ForgeRecipe& forgeRecipeFor(MiningToolType tool) { return kForgeRecipes[static_cast<size_t>(tool)]; }

bool canAffordForgeRecipe(const core::Inventory& inventory, MiningToolType tool) {
    const ForgeRecipe& recipe = forgeRecipeFor(tool);
    for (const ForgeRecipeCost& cost : recipe.costs) {
        if (cost.quantity <= 0) continue;
        if (core::totalQuantity(inventory, cost.oreType) < cost.quantity) return false;
    }
    return true;
}

bool isForgeRarityUnlocked(RarityTier playerUnlockedRarity, MiningToolType tool) {
    return rarityTierRank(playerUnlockedRarity) >= rarityTierRank(forgeRecipeFor(tool).requiredRarity);
}

ForgeResult forgeTool(core::Inventory& inventory, RarityTier playerUnlockedRarity, MiningToolType tool) {
    ForgeResult result;
    if (!isForgeRarityUnlocked(playerUnlockedRarity, tool)) return result;
    if (!canAffordForgeRecipe(inventory, tool)) return result;

    const ForgeRecipe& recipe = forgeRecipeFor(tool);
    for (const ForgeRecipeCost& cost : recipe.costs) {
        if (cost.quantity <= 0) continue;
        core::removeItem(inventory, cost.oreType, cost.quantity);
    }

    result.success = true;
    result.tool = tool;
    return result;
}

} // namespace engine::miningsim
