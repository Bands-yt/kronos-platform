#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/GameManifest.hpp"

namespace engine::core {

// Kronos ("Game Catalogue Overhaul", Phase 7): one real local game's
// already-computed inputs to the real Hidden Gems selection below --
// `qualityScore` from computeQualityScore() (core/QualityScore.hpp),
// `launchCount` the real number of net::GamePlaySession entries
// net::GamePlayLog has for this game (a real session, opened or closed,
// each counts once).
struct HiddenGemCandidate {
    GameManifest manifest;
    float qualityScore = 0.0f;
    int64_t launchCount = 0;
};

// A candidate needs at least this real QualityScore to be eligible at
// all -- "high-effort" per the user's own spec, not just "unpopular."
inline constexpr float kHiddenGemsQualityScoreFloor = 0.5f;

// Real, pure selection: eligible = qualityScore >= kHiddenGemsQualityScoreFloor
// AND launchCount at or below the real bottom-quartile launch count
// *among all of `candidates`* (not just the score-eligible ones -- the
// quartile threshold is computed once, over the whole real population,
// so filtering by score first can't shift what "low-player-count" means
// out from under it). Ties at the quartile boundary are included (a
// real "<=", not "<"). Result order matches `candidates`' own order
// (this function doesn't itself decide display ranking -- see
// core::QualityScore.hpp's own comment on what does).
[[nodiscard]] std::vector<GameManifest> selectHiddenGems(const std::vector<HiddenGemCandidate>& candidates);

// Kronos ("Game Catalogue Overhaul", Phase 7 -- "Monthly Hidden Gems
// job"): the real, honest "monthly job" for a local Alpha with no
// scheduler/cron infrastructure -- recomputed at Studio/engine_runtime
// startup by comparing real calendar months, not a simulated backend
// job. `unixSeconds` -> "YYYY-MM" via real UTC calendar math (the same
// honest day-bucketing approach core::computeGamePlayStats() already
// uses, extended one step to months) -- doesn't need a timezone-aware
// calendar library, a real month boundary is all a "once a month"
// cadence needs.
[[nodiscard]] std::string monthKeyForUnixSeconds(int64_t unixSeconds);

// True the real first time this is checked for a given calendar month
// (`lastComputedMonthKey` empty, or a real, different month than `now`
// falls in) -- a real, honest, idempotent-within-the-same-month gate, not
// a timer.
[[nodiscard]] bool shouldRecomputeHiddenGemsThisMonth(const std::string& lastComputedMonthKey, int64_t nowUnixSeconds);

} // namespace engine::core
