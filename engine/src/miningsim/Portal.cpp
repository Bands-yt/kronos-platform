#include "miningsim/Portal.hpp"

#include <array>

namespace engine::miningsim {

float portalSpawnWeight(RarityTier tier) {
    switch (tier) {
        case RarityTier::Common: return 100.0f;
        case RarityTier::Rare: return 55.0f;
        case RarityTier::Epic: return 25.0f;
        case RarityTier::Legendary: return 10.0f;
        // Real-"extremely rare" quartet, per the brief.
        case RarityTier::UltraLegendary: return 1.5f;
        case RarityTier::CorruptedHeavenly: return 0.6f;
        case RarityTier::Void: return 0.4f;
        case RarityTier::TrueHeavenly: return 0.1f;
    }
    return 100.0f;
}

RarityTier rollPortalTier(std::mt19937& rng) {
    std::array<RarityTier, kRarityTierCount> tiers{
        RarityTier::Common,        RarityTier::Rare,   RarityTier::Epic,      RarityTier::Legendary,
        RarityTier::UltraLegendary, RarityTier::CorruptedHeavenly, RarityTier::Void, RarityTier::TrueHeavenly,
    };
    std::array<float, kRarityTierCount> weights{};
    for (size_t i = 0; i < kRarityTierCount; ++i) weights[i] = portalSpawnWeight(tiers[i]);

    std::discrete_distribution<size_t> dist(weights.begin(), weights.end());
    return tiers[dist(rng)];
}

int rollPortalCountOnOpen(std::mt19937& rng) {
    std::uniform_int_distribution<int> extraDist(0, 2);
    return kMinPortalsPerOpening + extraDist(rng);
}

} // namespace engine::miningsim
