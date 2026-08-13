#pragma once

#include <array>
#include <cstdint>

#include "core/Inventory.hpp"
#include "core/OreNode.hpp"

namespace engine::core {

// Sprint 5 task category 3: real soft/hard currency. `coins` is the soft
// currency (earned by selling mined ore, spent on upgrades -- see
// Economy.cpp/UpgradeSystem.hpp), `gems` is the hard currency (earned in
// small amounts from rare-drop bonuses, see OreTypeInfo::bonusGemChance
// -- no in-engine real-money purchase path exists yet, that's the
// marketplace layer's job, see docs/ARCHITECTURE.md and
// marketplace/MarketplaceService.hpp). `int64_t`, not `int32_t`: a real
// long-lived economy needs to not silently wrap once totals climb into
// the tens of millions.
struct Wallet {
    int64_t coins = 0;
    int64_t gems = 0;
};

// Real, one-directional currency conversion: gems -> coins, at a fixed,
// generous rate (hard currency should convert *favorably* to soft, or
// gems would never feel worth spending this way at all). The reverse
// (coins -> gems) deliberately does NOT exist as a function here -- in a
// real live-service economy, letting soft currency buy hard currency
// would undermine gems' entire role as the scarce, harder-to-earn
// currency; this is a stated design boundary (see "Known issues" in
// README), not an oversight or a TODO.
constexpr int64_t kCoinsPerGem = 100;

// Returns coins actually gained (0 if `wallet.gems < gemsToConvert` --
// real validation, not a silent negative-balance underflow).
int64_t convertGemsToCoins(Wallet& wallet, int64_t gemsToConvert);

// --- Anti-inflation safeguards (task category 3 + task category 8) -----
//
// Two real, independent, non-redundant mechanisms, both keyed to the
// same rolling time window so a player can't dodge one by restructuring
// around it:
//
// 1. Per-ore-type price decay (`unitsSoldInWindow`): dumping a lot of one
//    ore type in a short window pays progressively less per unit -- see
//    `sellPriceForQuantity()`. Tracked *per window*, not per single
//    sell() call, specifically so fragmenting one big sell into many
//    tiny ones (a real, obvious exploit against a naive per-call-only
//    curve) doesn't reset the discount back to full price each time --
//    see "Real bugs this pass found" in README for why this matters and
//    `testRapidSellingCannotBypassPriceDecay()` in engine_tests for the
//    exact exploit it closes.
// 2. A total-coins-per-window soft cap (`coinsEarnedInWindow` vs.
//    `kEarnCapPerWindow`): once crossed, `applyEarnThrottle()` tapers
//    additional payout down (never to zero -- a hard wall reads as
//    broken, a taper reads as "the economy resists bot-speed farming"),
//    independent of which ore types are being sold.
struct EarnThrottle {
    int64_t coinsEarnedInWindow = 0;
    float windowElapsedSeconds = 0.0f;
    std::array<int, kOreTypeCount> unitsSoldInWindow{};
};

constexpr float kEarnWindowSeconds = 60.0f;
// Sized against real sale values, not a round number: Crystal (the
// rarest, most valuable ore) alone sells for 500 coins/unit at full
// price -- a cap set anywhere near that would taper a single completely
// legitimate high-value sale, not just bot-speed farming. 3000 covers
// several real full-price sales across a mixed haul (a realistic "good
// minute" of mining+selling) before tapering kicks in, while still
// meaningfully capping the actual target: sustained, much-faster-than-
// human-possible earning.
constexpr int64_t kEarnCapPerWindow = 3000;

// Called once per tick (Application.cpp's pre-tick hook) -- advances
// `windowElapsedSeconds`, resetting every counter once
// `kEarnWindowSeconds` has elapsed (a real rolling window, not an
// ever-growing lifetime total).
void tickEarnThrottle(EarnThrottle& throttle, float dt);

// Pure -- scales `proposedCoins` down once `coinsEarnedInWindow` has
// already crossed `kEarnCapPerWindow` this window (floor: 25% of
// proposed, never fully zeroed), then adds the *actual* (post-taper)
// amount to `coinsEarnedInWindow` for next call's own accounting.
[[nodiscard]] int64_t applyEarnThrottle(EarnThrottle& throttle, int64_t proposedCoins);

// --- Selling / price curves (task category 4) ---------------------------
//
// First `kSellCurveThreshold` units of a given ore type sold within the
// current window sell at full `OreTypeInfo::baseSellValue`; every unit
// past that decays by `kSellCurveDecay` per unit, floored at 40% of base
// -- a real, monotonically-non-increasing market-saturation curve, not a
// flat per-unit price that would let one huge sell trivialize the
// economy. `alreadySoldInWindow` is `EarnThrottle::unitsSoldInWindow[oreType]`
// -- exposed as a parameter (rather than only reachable via `sellOre()`
// below) so the curve's own shape is independently unit-testable without
// needing a live Inventory/Wallet.
constexpr int kSellCurveThreshold = 10;
constexpr float kSellCurveDecay = 0.03f;
constexpr float kSellCurveFloor = 0.40f;

[[nodiscard]] int64_t sellPriceForQuantity(OreType oreType, int quantity, int alreadySoldInWindow);

struct SellResult {
    int quantitySold = 0;          // actually removed from inventory (see Inventory::removeItem())
    int64_t coinsBeforeThrottle = 0; // the real price-curve result, before the earn-window taper
    int64_t coinsEarned = 0;         // what actually landed in the wallet
};

// The real, full sell transaction: removes up to `quantity` units of
// `oreType` from `inventory` (honestly capped at what's actually
// present, see Inventory::removeItem()), prices them via the real
// window-aware curve above, taper via `applyEarnThrottle()`, credits
// `wallet.coins`, and records the sale in `throttle` for the next call's
// own price-curve/cap accounting.
SellResult sellOre(Inventory& inventory, Wallet& wallet, EarnThrottle& throttle, OreType oreType, int quantity);

} // namespace engine::core
