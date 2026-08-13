#include "anticheat/CurrencyAnomalyDetector.hpp"

namespace engine::anticheat {

void CurrencyAnomalyDetector::recordSale(PlayerId player, int64_t proposedCoins, int64_t actualCoins, float nowSeconds) {
    if (actualCoins < proposedCoins) counter_.recordEvent(player, nowSeconds);
}

} // namespace engine::anticheat
