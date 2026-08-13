#include "analytics/TelemetryQueue.hpp"

#include <algorithm>

namespace engine::analytics {

void TelemetryQueue::push(TelemetryEvent event) {
    std::lock_guard<std::mutex> lock(mutex_);
    events_.push_back(std::move(event));
}

std::vector<TelemetryEvent> TelemetryQueue::drain(size_t maxCount) {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t count = std::min(maxCount, events_.size());

    std::vector<TelemetryEvent> result(
        std::make_move_iterator(events_.begin()),
        std::make_move_iterator(events_.begin() + static_cast<long>(count)));
    events_.erase(events_.begin(), events_.begin() + static_cast<long>(count));
    return result;
}

size_t TelemetryQueue::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return events_.size();
}

} // namespace engine::analytics
