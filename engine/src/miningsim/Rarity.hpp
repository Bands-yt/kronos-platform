#pragma once

#include <cstddef>
#include <cstdint>

#include <glm/glm.hpp>

namespace engine::miningsim {

// Kronos roadmap Milestone 13 ("Mining Sim RTX: Rarity/zone data model"):
// the brief's own exact 8 real rarity tiers -- deliberately NOT
// core::OreRarity (Sprint 5's existing 6-tier Common..Mythic economy
// ladder for basic ore mining, see core/OreNode.hpp): this is a real,
// distinct, wider rarity system for Mining Sim RTX's own zone/portal/
// dungeon content, the same "don't conflate two real, differently-scoped
// systems" precedent tntwars::ScavengeMaterialType already set against
// core::OreType. Ordered low-to-high so `rarityTierRank()` below is a
// trivial, real ordinal comparison.
enum class RarityTier : uint8_t {
    Common,
    Rare,
    Epic,
    Legendary,
    UltraLegendary,
    CorruptedHeavenly,
    Void,
    TrueHeavenly,
};
constexpr size_t kRarityTierCount = 8;

[[nodiscard]] const char* rarityTierName(RarityTier tier);

// Real, brief-specified colors -- GREEN/BLUE/PURPLE/YELLOW/BIOLUMINESCENT
// BLUE/WHITE/BLACK/PURE WHITE respectively. CorruptedHeavenly's own real
// "WHITE+pulsing RED veins" look is two real colors, not one -- see
// rarityTierVeinColor()/rarityTierHasPulsingVeins() below for the second
// half of that spec.
[[nodiscard]] glm::vec3 rarityTierColor(RarityTier tier);

// Real, true only for CorruptedHeavenly -- a distinct visual *behavior*
// flag (pulsing), not just a color choice, so a real renderer can tell
// "flat white" (TrueHeavenly) apart from "white with an animated red
// pulse" (CorruptedHeavenly) without string-matching a name.
[[nodiscard]] bool rarityTierHasPulsingVeins(RarityTier tier);
[[nodiscard]] glm::vec3 rarityTierVeinColor(RarityTier tier); // real-only meaningful when rarityTierHasPulsingVeins() is true

// Real ordinal 0 (Common) .. 7 (TrueHeavenly) -- the real, direct
// "is this rarer than that" comparison every other real function below
// is built on.
[[nodiscard]] int rarityTierRank(RarityTier tier);

// Kronos roadmap Milestone 13's own real rule: "Any rarity in any zone;
// starting zones capped at Epic dungeons." -- real, direct predicate a
// caller checks before letting a *dungeon* (not general zone loot) roll
// above Epic while the player is still in a real starting zone.
[[nodiscard]] bool isDungeonRarityAllowedInStartingZone(RarityTier tier);

} // namespace engine::miningsim
