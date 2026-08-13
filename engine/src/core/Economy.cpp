#include "core/Economy.hpp"

#include <algorithm>
#include <cmath>

namespace engine::core {

int64_t convertGemsToCoins(Wallet& wallet, int64_t gemsToConvert) {
    if (gemsToConvert <= 0 || gemsToConvert > wallet.gems) return 0;
    wallet.gems -= gemsToConvert;
    int64_t coinsGained = gemsToConvert * kCoinsPerGem;
    wallet.coins += coinsGained;
    return coinsGained;
}

void tickEarnThrottle(EarnThrottle& throttle, float dt) {
    throttle.windowElapsedSeconds += dt;
    if (throttle.windowElapsedSeconds >= kEarnWindowSeconds) {
        throttle.windowElapsedSeconds = 0.0f;
        throttle.coinsEarnedInWindow = 0;
        throttle.unitsSoldInWindow.fill(0);
    }
}

int64_t applyEarnThrottle(EarnThrottle& throttle, int64_t proposedCoins) {
    if (proposedCoins <= 0) return 0;

    int64_t actual = proposedCoins;
    if (throttle.coinsEarnedInWindow >= kEarnCapPerWindow) {
        // Already over the cap this window -- taper hard, but never to
        // literal zero (a hard wall would read as a bug, not a
        // deliberate economic safeguard).
        actual = std::max<int64_t>(1, static_cast<int64_t>(std::llround(static_cast<double>(proposedCoins) * 0.25)));
    } else if (throttle.coinsEarnedInWindow + proposedCoins > kEarnCapPerWindow) {
        // Straddles the cap: the portion under the cap pays in full, the
        // overflow portion tapers -- avoids a discontinuous "last coin
        // before the cap is worth 100x the first coin after it" cliff.
        int64_t underCap = kEarnCapPerWindow - throttle.coinsEarnedInWindow;
        int64_t overCap = proposedCoins - underCap;
        int64_t taperedOver = std::max<int64_t>(1, static_cast<int64_t>(std::llround(static_cast<double>(overCap) * 0.25)));
        actual = underCap + taperedOver;
    }

    throttle.coinsEarnedInWindow += actual;
    return actual;
}

int64_t sellPriceForQuantity(OreType oreType, int quantity, int alreadySoldInWindow) {
    if (quantity <= 0) return 0;
    int64_t base = oreTypeInfo(oreType).baseSellValue;

    int64_t total = 0;
    for (int i = 1; i <= quantity; ++i) {
        int unitIndexInWindow = alreadySoldInWindow + i;
        float multiplier = 1.0f;
        if (unitIndexInWindow > kSellCurveThreshold) {
            multiplier = std::max(
                kSellCurveFloor, 1.0f - kSellCurveDecay * static_cast<float>(unitIndexInWindow - kSellCurveThreshold));
        }
        total += static_cast<int64_t>(std::llround(static_cast<float>(base) * multiplier));
    }
    return total;
}

SellResult sellOre(Inventory& inventory, Wallet& wallet, EarnThrottle& throttle, OreType oreType, int quantity) {
    SellResult result;
    int actualQuantity = removeItem(inventory, oreType, quantity);
    if (actualQuantity <= 0) return result;

    int alreadySold = throttle.unitsSoldInWindow[static_cast<size_t>(oreType)];
    int64_t rawPrice = sellPriceForQuantity(oreType, actualQuantity, alreadySold);
    int64_t earned = applyEarnThrottle(throttle, rawPrice);

    throttle.unitsSoldInWindow[static_cast<size_t>(oreType)] += actualQuantity;
    wallet.coins += earned;

    result.quantitySold = actualQuantity;
    result.coinsBeforeThrottle = rawPrice;
    result.coinsEarned = earned;
    return result;
}

} // namespace engine::core
