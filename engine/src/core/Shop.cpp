#include "core/Shop.hpp"

namespace engine::core {

SellAllResult sellAllInventory(Inventory& inventory, Wallet& wallet, EarnThrottle& throttle) {
    SellAllResult result;
    for (size_t i = 0; i < kOreTypeCount; ++i) {
        OreType type = static_cast<OreType>(i);
        int have = totalQuantity(inventory, type);
        if (have <= 0) continue;

        SellResult sold = sellOre(inventory, wallet, throttle, type, have);
        result.quantitiesSold[i] = sold.quantitySold;
        result.totalCoinsEarned += sold.coinsEarned;
    }
    return result;
}

} // namespace engine::core
