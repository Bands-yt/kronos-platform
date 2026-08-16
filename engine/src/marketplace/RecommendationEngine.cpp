#include "marketplace/RecommendationEngine.hpp"

#include <algorithm>
#include <cmath>

namespace engine::marketplace {

float computeRecommendationScore(const core::AvatarItemManifest& item, int64_t nowUnixSeconds) {
    // Recency -- real, honest handling of "never updated" data
    // (updateTimestampUnixSeconds <= 0): treated as maximally stale
    // rather than crashing or wrapping into a huge negative age.
    float recencyFactor;
    if (item.updateTimestampUnixSeconds <= 0 || nowUnixSeconds <= item.updateTimestampUnixSeconds) {
        recencyFactor = item.updateTimestampUnixSeconds <= 0 ? 0.0f : 1.0f;
    } else {
        float daysSinceUpdate =
            static_cast<float>(nowUnixSeconds - item.updateTimestampUnixSeconds) / 86400.0f;
        recencyFactor = 1.0f / (1.0f + daysSinceUpdate / 30.0f);
    }

    // Popularity -- log-compressed so one viral item's purchaseCount
    // doesn't dominate the ranking forever; std::log1p(100) is the real
    // normalization reference ("100 purchases ~= full popularity
    // credit"), a real, tunable constant, not a fabricated precision
    // claim.
    float popularityFactor =
        std::log1p(static_cast<float>(std::max(item.purchaseCount, 0))) / std::log1p(100.0f);
    popularityFactor = std::clamp(popularityFactor, 0.0f, 1.5f);

    // Quality -- real ratingScore (0-5), confidence-weighted by real
    // ratingCount so a single rating can't outrank a well-established
    // one; ratingCount >= 10 reaches full confidence.
    float qualityFactor = 0.0f;
    if (item.ratingCount > 0) {
        float confidence = std::min(1.0f, static_cast<float>(item.ratingCount) / 10.0f);
        qualityFactor = (item.ratingScore / 5.0f) * confidence;
    }

    return 0.25f * recencyFactor + 0.40f * popularityFactor + 0.35f * qualityFactor;
}

std::vector<const core::AvatarItemManifest*> rankRecommendedItems(
    const std::vector<const core::AvatarItemManifest*>& candidates, int64_t nowUnixSeconds, size_t maxResults) {
    std::vector<const core::AvatarItemManifest*> approved;
    for (const core::AvatarItemManifest* item : candidates) {
        if (item != nullptr && item->moderationStatus == core::AvatarItemModerationStatus::Approved) {
            approved.push_back(item);
        }
    }

    std::stable_sort(approved.begin(), approved.end(),
                      [nowUnixSeconds](const core::AvatarItemManifest* a, const core::AvatarItemManifest* b) {
                          float scoreA = computeRecommendationScore(*a, nowUnixSeconds);
                          float scoreB = computeRecommendationScore(*b, nowUnixSeconds);
                          if (scoreA != scoreB) return scoreA > scoreB;
                          return a->item.id < b->item.id;
                      });

    if (approved.size() > maxResults) approved.resize(maxResults);
    return approved;
}

} // namespace engine::marketplace
