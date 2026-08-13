#include "miningsim/Rarity.hpp"

namespace engine::miningsim {

const char* rarityTierName(RarityTier tier) {
    switch (tier) {
        case RarityTier::Common: return "Common";
        case RarityTier::Rare: return "Rare";
        case RarityTier::Epic: return "Epic";
        case RarityTier::Legendary: return "Legendary";
        case RarityTier::UltraLegendary: return "Ultra Legendary";
        case RarityTier::CorruptedHeavenly: return "Corrupted Heavenly";
        case RarityTier::Void: return "Void";
        case RarityTier::TrueHeavenly: return "True Heavenly";
    }
    return "Common";
}

glm::vec3 rarityTierColor(RarityTier tier) {
    switch (tier) {
        case RarityTier::Common: return {0.20f, 0.80f, 0.25f};             // GREEN
        case RarityTier::Rare: return {0.20f, 0.45f, 0.95f};               // BLUE
        case RarityTier::Epic: return {0.60f, 0.20f, 0.85f};               // PURPLE
        case RarityTier::Legendary: return {0.95f, 0.85f, 0.15f};          // YELLOW
        case RarityTier::UltraLegendary: return {0.15f, 0.95f, 0.90f};     // BIOLUMINESCENT BLUE
        case RarityTier::CorruptedHeavenly: return {0.95f, 0.95f, 0.95f};  // WHITE (+ pulsing red veins, see below)
        case RarityTier::Void: return {0.02f, 0.02f, 0.03f};               // BLACK
        case RarityTier::TrueHeavenly: return {1.0f, 1.0f, 1.0f};          // PURE WHITE
    }
    return {0.20f, 0.80f, 0.25f};
}

bool rarityTierHasPulsingVeins(RarityTier tier) { return tier == RarityTier::CorruptedHeavenly; }

glm::vec3 rarityTierVeinColor(RarityTier tier) {
    return tier == RarityTier::CorruptedHeavenly ? glm::vec3{0.90f, 0.05f, 0.05f} : glm::vec3{0.0f};
}

int rarityTierRank(RarityTier tier) { return static_cast<int>(tier); }

bool isDungeonRarityAllowedInStartingZone(RarityTier tier) {
    return rarityTierRank(tier) <= rarityTierRank(RarityTier::Epic);
}

} // namespace engine::miningsim
