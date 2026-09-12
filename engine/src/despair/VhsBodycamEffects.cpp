#include "despair/VhsBodycamEffects.hpp"

#include <algorithm>

namespace engine::despair {

float computeVhsStaticNoiseIntensity(float nearestThreatDistance, float burstRadius, float fadeRadius) {
    if (nearestThreatDistance < 0.0f) return 0.0f;
    if (nearestThreatDistance <= burstRadius) return 1.0f;
    if (nearestThreatDistance >= fadeRadius) return 0.0f;

    float t = (nearestThreatDistance - burstRadius) / (fadeRadius - burstRadius);
    return std::clamp(1.0f - t, 0.0f, 1.0f);
}

} // namespace engine::despair
