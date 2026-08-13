#include "tntwars/Crafting.hpp"

namespace engine::tntwars {

namespace {
constexpr ExplosiveRecipeInfo kExplosiveRecipeInfos[] = {
    {
        ExplosiveRecipeType::StandardCharge,
        "Standard Charge",
        {{{ScavengeMaterialType::ScrapMetal, 3}, {ScavengeMaterialType::BlastingPowder, 2}, {}}},
        4.0f,
        10.0f,
        220.0f,
        28.0f,
    },
    {
        ExplosiveRecipeType::MegaCharge,
        "Mega Charge",
        {{{ScavengeMaterialType::ScrapMetal, 6}, {ScavengeMaterialType::BlastingPowder, 4},
          {ScavengeMaterialType::Wiring, 3}}},
        6.0f,
        16.0f,
        400.0f,
        45.0f,
    },
    {
        ExplosiveRecipeType::AdvancedCharge,
        "Advanced Charge",
        {{{ScavengeMaterialType::ScrapMetal, 10}, {ScavengeMaterialType::BlastingPowder, 8},
          {ScavengeMaterialType::Wiring, 6}}},
        8.0f,
        22.0f,
        650.0f,
        70.0f,
    },
};
} // namespace

const ExplosiveRecipeInfo& explosiveRecipeInfo(ExplosiveRecipeType type) {
    return kExplosiveRecipeInfos[static_cast<size_t>(type)];
}

bool canAffordRecipe(const ScavengedMaterials& materials, ExplosiveRecipeType recipe) {
    const ExplosiveRecipeInfo& info = explosiveRecipeInfo(recipe);
    for (const ExplosiveRecipeCost& cost : info.costs) {
        if (cost.quantity <= 0) continue;
        if (scavengedMaterialCount(materials, cost.material) < cost.quantity) return false;
    }
    return true;
}

bool craftExplosive(ScavengedMaterials& materials, CraftedExplosives& crafted, ExplosiveRecipeType recipe) {
    if (!canAffordRecipe(materials, recipe)) return false;
    const ExplosiveRecipeInfo& info = explosiveRecipeInfo(recipe);
    for (const ExplosiveRecipeCost& cost : info.costs) {
        if (cost.quantity <= 0) continue;
        removeScavengedMaterial(materials, cost.material, cost.quantity);
    }
    crafted.counts[static_cast<size_t>(recipe)] += 1;
    return true;
}

} // namespace engine::tntwars
