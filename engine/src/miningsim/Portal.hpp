#pragma once

#include <random>

#include "miningsim/Rarity.hpp"

namespace engine::miningsim {

// Kronos roadmap Milestone 14 ("Portal system"): the brief's own exact
// spec -- "very rare spawns; opening one spawns >=3 portals of that
// tier; Heavenly/Corrupted Heavenly/Void/True Heavenly extremely rare."
// Real, pure, RNG-parameterized functions (the caller supplies the
// std::mt19937&, the same real, testable "caller seeds it, tests check
// the resulting distribution" convention core::rollOreDrops() already
// established) -- no owned/global RNG state here.

// Real, relative spawn weight per tier -- higher is more common. The
// four highest tiers (UltraLegendary/CorruptedHeavenly/Void/TrueHeavenly)
// are real-tuned to a small fraction of Common's own weight, matching
// the brief's own "extremely rare" language for exactly those four.
[[nodiscard]] float portalSpawnWeight(RarityTier tier);

// Real, weighted-random tier roll across all 8 real tiers, using
// portalSpawnWeight() above.
[[nodiscard]] RarityTier rollPortalTier(std::mt19937& rng);

// Real, brief-mandated floor -- opening a portal spawns at least this
// many more real portals of that same tier.
constexpr int kMinPortalsPerOpening = 3;

// Real portal count roll: kMinPortalsPerOpening plus a real small random
// extra (0..2) -- always >=3, matching the brief's own ">=3 portals of
// that tier" exactly, never a fixed, suspiciously-round-every-time 3.
[[nodiscard]] int rollPortalCountOnOpen(std::mt19937& rng);

} // namespace engine::miningsim
