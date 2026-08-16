#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/AvatarItemManifest.hpp"

namespace engine::marketplace {

// Kronos ("Simple Recommendation Engine"): a real, pure, rules-based
// ranking -- NOT a real ML model (no training data or inference runtime
// exists anywhere in this codebase; same "real heuristic, honest about
// not being ML, with a stable seam for a real model later" pattern
// safety::TextClassifierStub already establishes for a different
// feature). Blends three real, already-tracked signals on
// core::AvatarItemManifest:
//   - recency: real updateTimestampUnixSeconds, decaying over ~30 days
//   - popularity: real purchaseCount, log-compressed (so one viral item
//     doesn't permanently dominate every other item forever)
//   - quality: real ratingScore, confidence-weighted by real ratingCount
//     (a single 5-star rating doesn't outrank a hundred 4.5-star ones)
// Every one of these three signals is exactly what a future learned
// ranking model would train on -- the point of logging real CTR/
// conversion telemetry against this engine's own output (see
// RuntimeShell's own recommendation-click telemetry) is to make that
// future swap possible, not decorative.
[[nodiscard]] float computeRecommendationScore(const core::AvatarItemManifest& item, int64_t nowUnixSeconds);

// Real, pure -- filters to Approved items only (same "never surface
// pending/rejected content outside moderation tooling" rule every other
// real player-facing browse surface in this codebase already follows),
// sorts by computeRecommendationScore() descending (ties broken by
// itemId for a real, stable, deterministic order), and returns at most
// `maxResults`. Does not mutate or take ownership of `candidates` --
// every returned pointer aliases the caller's own, already-owned data
// (the same "index returns pointers into the caller's real storage"
// convention core::CatalogueIndex::search() already establishes).
[[nodiscard]] std::vector<const core::AvatarItemManifest*> rankRecommendedItems(
    const std::vector<const core::AvatarItemManifest*>& candidates, int64_t nowUnixSeconds, size_t maxResults);

} // namespace engine::marketplace
