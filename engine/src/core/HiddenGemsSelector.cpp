#include "core/HiddenGemsSelector.hpp"

#include <algorithm>
#include <cstdio>
#include <ctime>

namespace engine::core {

namespace {
// gmtime_r is POSIX-only; MSVC's equivalent is gmtime_s, with the
// arguments reversed (result first, then the time_t to convert).
void gmtimePortable(const std::time_t* time, std::tm* out) {
#if defined(_WIN32)
    gmtime_s(out, time);
#else
    gmtime_r(time, out);
#endif
}
} // namespace

std::vector<GameManifest> selectHiddenGems(const std::vector<HiddenGemCandidate>& candidates) {
    if (candidates.empty()) return {};

    std::vector<int64_t> launchCounts;
    launchCounts.reserve(candidates.size());
    for (const auto& c : candidates) launchCounts.push_back(c.launchCount);
    std::sort(launchCounts.begin(), launchCounts.end());
    // Real bottom-quartile threshold over the whole real population --
    // see this function's own header comment on why the quartile isn't
    // computed after filtering by score.
    int64_t quartileThreshold = launchCounts[launchCounts.size() / 4];

    std::vector<GameManifest> selected;
    for (const auto& c : candidates) {
        if (c.qualityScore >= kHiddenGemsQualityScoreFloor && c.launchCount <= quartileThreshold) {
            selected.push_back(c.manifest);
        }
    }
    return selected;
}

std::string monthKeyForUnixSeconds(int64_t unixSeconds) {
    std::time_t time = static_cast<std::time_t>(unixSeconds);
    std::tm tmValue{};
    gmtimePortable(&time, &tmValue);
    char buffer[8]; // "YYYY-MM\0"
    std::snprintf(buffer, sizeof(buffer), "%04d-%02d", tmValue.tm_year + 1900, tmValue.tm_mon + 1);
    return buffer;
}

bool shouldRecomputeHiddenGemsThisMonth(const std::string& lastComputedMonthKey, int64_t nowUnixSeconds) {
    return lastComputedMonthKey.empty() || lastComputedMonthKey != monthKeyForUnixSeconds(nowUnixSeconds);
}

} // namespace engine::core
