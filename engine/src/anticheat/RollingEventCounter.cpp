#include "anticheat/RollingEventCounter.hpp"

namespace engine::anticheat {

void RollingEventCounter::recordEvent(PlayerId player, float nowSeconds) {
    eventTimestamps_[player].push_back(nowSeconds);
}

size_t RollingEventCounter::countInWindow(PlayerId player, float nowSeconds) {
    auto it = eventTimestamps_.find(player);
    if (it == eventTimestamps_.end()) return 0;

    std::deque<float>& timestamps = it->second;
    float cutoff = nowSeconds - windowSeconds_;
    while (!timestamps.empty() && timestamps.front() < cutoff) timestamps.pop_front();
    return timestamps.size();
}

bool RollingEventCounter::isSuspicious(PlayerId player, float nowSeconds, size_t threshold) {
    return countInWindow(player, nowSeconds) >= threshold;
}

} // namespace engine::anticheat
