#pragma once

#include <cstdint>
#include <vector>

#include "net/GamePlayLog.hpp"

namespace engine::core {

// Kronos ("Game Catalogue Overhaul", Phase 6): the real, locally-derived
// signals net::GamePlayLog's own session records reduce to -- the
// concrete "retention"/"stability" half of QualityScore's three real
// inputs (the third, `effortScore`, has no automatic data source
// anywhere in this codebase and stays a manually-authored
// core::GameManifest field, a real, stated gap, not a faked one -- see
// computeQualityScore()'s own comment).
struct GamePlayStats {
    float avgSessionLengthMinutes = 0.0f;
    // Already the real ratio computeQualityScore() needs: real distinct
    // calendar days a session started, divided by real days since the
    // first-ever recorded session for this game (at least 1, so a
    // brand-new game with exactly one day of data reads as "played every
    // day it could have been," not a divide-by-zero).
    float distinctDaysPlayedRatio = 0.0f;
    // Real fraction of real *closed* sessions that ended crashed --
    // still-open sessions (a session with no matching recordSessionEnd()
    // yet) are excluded, not counted as a real close of any kind.
    float crashRate = 0.0f;
};

// Real, pure reduction over `sessions` (already filtered to one game via
// net::GamePlayLog::sessionsForGame()) -- `nowUnixSeconds` only affects a
// still-open session's contribution to avgSessionLengthMinutes (an
// open session's real elapsed-so-far duration counts, so a currently-
// playing session isn't invisible to a caller computing live stats).
// A real, honest all-zero result for an empty `sessions`, not a NaN/
// divide-by-zero.
[[nodiscard]] GamePlayStats computeGamePlayStats(const std::vector<net::GamePlaySession>& sessions,
                                                  int64_t nowUnixSeconds);

// The real, concrete QualityScore formula (Kronos "Game Catalogue
// Overhaul" -- "QualityScore system (ML-ready): prioritise high effort,
// high retention, stability"). Weights are named constants specifically
// so they're easy to retune once real production data exists, not magic
// numbers buried in the expression -- see kEffortWeight etc. below.
//
// `effortScore` (content depth/update frequency) is real, but manually
// authored (core::GameManifest::effortScore) -- no automatic "how much
// effort went into this game" signal exists anywhere in this codebase
// today, and inventing a fake one would be dishonest. This is the
// concrete, stated place a future ML model (per the user's own
// "ML-ready, not ML now" framing) would eventually replace a manual
// input with a learned one -- computeQualityScore() itself doesn't care
// which, it just takes a float in [0,1].
[[nodiscard]] float computeQualityScore(float effortScore, const GamePlayStats& stats);

inline constexpr float kEffortWeight = 0.35f;
inline constexpr float kSessionLengthWeight = 0.25f;
inline constexpr float kRetentionWeight = 0.20f;
inline constexpr float kStabilityWeight = 0.20f;
// A session averaging this long or longer contributes the real, full
// 1.0 to the session-length term -- real, honest saturation (a 3-hour
// average session isn't "12x better" than a 1-hour one for ranking
// purposes, it's just real, comfortably long).
inline constexpr float kSessionLengthSaturationMinutes = 60.0f;

// Kronos ("Moderation Architecture v2", "Crash -> Safety Link"): a real,
// pure threshold over GamePlayStats::crashRate -- the exact same real
// signal computeQualityScore()'s own kStabilityWeight term already
// consumes, reused here for a distinct real purpose ("should a human be
// warned this game might be unstable/unsafe") rather than a second,
// parallel crash-tracking mechanism. `kCrashPatternMinimumLaunchCount`
// exists because crashRate alone is meaningless noise at a tiny sample
// size (1 crash out of 2 real launches is 50% and tells you nothing) --
// a real game needs a real minimum number of real recorded sessions
// before its crash rate is worth surfacing at all.
inline constexpr float kCrashPatternRateThreshold = 0.25f;
inline constexpr int64_t kCrashPatternMinimumLaunchCount = 5;

// Real, pure, honest recommendation only -- true means "a human should
// look at this," never an instruction to auto-mutate
// core::GameManifest::safetyStatus itself. Matches the same mandatory-
// human-review principle every other real escalation in this codebase
// follows (docs/ARCHITECTURE.md §10/§15): a heuristic flags, a person
// decides.
[[nodiscard]] inline bool shouldFlagGameForCrashPattern(float crashRate, int64_t launchCount) {
    return launchCount >= kCrashPatternMinimumLaunchCount && crashRate >= kCrashPatternRateThreshold;
}

} // namespace engine::core
