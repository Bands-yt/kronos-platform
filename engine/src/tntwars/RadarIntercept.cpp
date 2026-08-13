#include "tntwars/RadarIntercept.hpp"

namespace engine::tntwars {

bool tryInterceptBlip(RadarBlip& blip, float nowSeconds) {
    if (blip.intercepted || blip.expired) return false; // real, one-shot -- already resolved
    float elapsed = nowSeconds - blip.spawnTimeSeconds;
    if (elapsed < 0.0f || elapsed > RadarBlip::kInterceptWindowSeconds) return false;
    blip.intercepted = true;
    return true;
}

void tickRadarBlipExpiry(RadarBlip& blip, float nowSeconds) {
    if (blip.intercepted || blip.expired) return;
    float elapsed = nowSeconds - blip.spawnTimeSeconds;
    if (elapsed > RadarBlip::kInterceptWindowSeconds) blip.expired = true;
}

} // namespace engine::tntwars
