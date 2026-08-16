#include "core/QualityScore.hpp"

#include <algorithm>
#include <set>

namespace engine::core {

namespace {
// Real, honest UTC calendar-day bucketing -- integral division by a
// day's real second count. Doesn't need a timezone-aware calendar
// library: "did the player come back on a different day" only needs a
// consistent, real bucket boundary, not a display-accurate local date.
constexpr int64_t kSecondsPerDay = 86400;
int64_t dayBucket(int64_t unixSeconds) { return unixSeconds / kSecondsPerDay; }
} // namespace

GamePlayStats computeGamePlayStats(const std::vector<net::GamePlaySession>& sessions, int64_t nowUnixSeconds) {
    GamePlayStats stats;
    if (sessions.empty()) return stats;

    double totalMinutes = 0.0;
    int sessionsWithDuration = 0;
    int closedSessions = 0;
    int crashedSessions = 0;
    std::set<int64_t> distinctDayBuckets;
    int64_t earliestStart = sessions.front().startUnixSeconds;

    for (const auto& session : sessions) {
        earliestStart = std::min(earliestStart, session.startUnixSeconds);
        distinctDayBuckets.insert(dayBucket(session.startUnixSeconds));

        int64_t effectiveEnd = session.endUnixSeconds > 0 ? session.endUnixSeconds : nowUnixSeconds;
        int64_t durationSeconds = effectiveEnd - session.startUnixSeconds;
        if (durationSeconds > 0) {
            totalMinutes += static_cast<double>(durationSeconds) / 60.0;
            ++sessionsWithDuration;
        }

        if (session.endUnixSeconds > 0) {
            ++closedSessions;
            if (session.crashed) ++crashedSessions;
        }
    }

    stats.avgSessionLengthMinutes =
        sessionsWithDuration > 0 ? static_cast<float>(totalMinutes / static_cast<double>(sessionsWithDuration)) : 0.0f;

    int64_t daysSinceFirstLaunch = std::max<int64_t>(1, dayBucket(nowUnixSeconds) - dayBucket(earliestStart) + 1);
    stats.distinctDaysPlayedRatio =
        std::min(1.0f, static_cast<float>(distinctDayBuckets.size()) / static_cast<float>(daysSinceFirstLaunch));

    stats.crashRate = closedSessions > 0 ? static_cast<float>(crashedSessions) / static_cast<float>(closedSessions) : 0.0f;

    return stats;
}

float computeQualityScore(float effortScore, const GamePlayStats& stats) {
    float sessionLengthTerm = std::clamp(stats.avgSessionLengthMinutes / kSessionLengthSaturationMinutes, 0.0f, 1.0f);
    float retentionTerm = std::clamp(stats.distinctDaysPlayedRatio, 0.0f, 1.0f);
    float stabilityTerm = 1.0f - std::clamp(stats.crashRate, 0.0f, 1.0f);
    float effortTerm = std::clamp(effortScore, 0.0f, 1.0f);

    return kEffortWeight * effortTerm + kSessionLengthWeight * sessionLengthTerm + kRetentionWeight * retentionTerm +
           kStabilityWeight * stabilityTerm;
}

} // namespace engine::core
