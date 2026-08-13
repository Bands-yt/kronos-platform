#pragma once

#include <array>
#include <cstdint>

#include "tntwars/Scavenging.hpp"
#include "tntwars/TntCharge.hpp"

namespace engine::tntwars {

// Kronos roadmap Milestone 6 ("Crafting explosives"): real recipes that
// turn Milestone 5's scavenged materials into real charges strictly
// stronger than the always-available, unlimited "basic" charge
// TntWarsMatch::placeTntCharge(player, position) already provides (see
// Milestone 3) -- crafting is a real, optional power investment (bigger
// blast radius/damage/impulse, at the real cost of a real, longer fuse
// and real scavenged materials), not a gate on the core TNT mechanic
// itself.
// AdvancedCharge (Kronos roadmap Milestone 12, Space map's own "better
// explosives (better materials)") is real-strictly stronger than
// MegaCharge, at the real cost of more of every existing real scavenged
// material rather than a new resource type -- "better materials" read
// honestly as "a bigger real material investment," not an invented
// fourth resource this session has no other real use for.
enum class ExplosiveRecipeType : uint8_t { StandardCharge, MegaCharge, AdvancedCharge };
constexpr size_t kExplosiveRecipeTypeCount = 3;

struct ExplosiveRecipeCost {
    ScavengeMaterialType material = ScavengeMaterialType::ScrapMetal;
    int quantity = 0; // real-0 means "this ingredient slot is unused"
};

// Up to 3 real ingredient lines per recipe -- an unused slot has
// quantity=0 and is real-skipped by canAffordRecipe()/craftExplosive()
// below, so a 1- or 2-ingredient recipe doesn't need special-casing.
constexpr size_t kMaxRecipeIngredients = 3;

struct ExplosiveRecipeInfo {
    ExplosiveRecipeType type;
    const char* name;
    std::array<ExplosiveRecipeCost, kMaxRecipeIngredients> costs{};
    float fuseSeconds = kDefaultFuseSeconds;
    float explosionRadius = kExplosionRadius;
    float explosionMaxDamage = kExplosionMaxDamage;
    float explosionMaxImpulse = kExplosionMaxImpulse;
};
[[nodiscard]] const ExplosiveRecipeInfo& explosiveRecipeInfo(ExplosiveRecipeType type);

[[nodiscard]] bool canAffordRecipe(const ScavengedMaterials& materials, ExplosiveRecipeType recipe);

// Real, small per-player carried-crafted-explosives ledger -- a crafted
// charge sits here (real inventory, matching ScavengedMaterials' own
// flat-count shape) until a real caller spends one via TntWarsMatch's own
// recipe-aware placeTntCharge() overload.
struct CraftedExplosives {
    std::array<int, kExplosiveRecipeTypeCount> counts{};
};

// Real craft attempt: fails (returns false, no state change at all) if
// `materials` can't real-afford every real ingredient line -- otherwise
// real-consumes exactly the recipe's own cost and real-credits one unit
// of `recipe` to `crafted`.
bool craftExplosive(ScavengedMaterials& materials, CraftedExplosives& crafted, ExplosiveRecipeType recipe);

} // namespace engine::tntwars
