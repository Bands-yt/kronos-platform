#pragma once

#include <mutex>
#include <vector>

#include "analytics/TelemetryEvent.hpp"

namespace engine::analytics {

// The one shared queue every subsystem's telemetry funnels through --
// §13: "the same one Studio's own performance profiler uses." Producers
// (the render graph's frame-budget sampler, §4.1; crash reports; input-
// method tracking) call push() from whatever thread they run on;
// TelemetrySender drains it periodically from a dedicated send thread, so
// this needs to be genuinely thread-safe, not just structurally shaped
// like a queue.
class TelemetryQueue {
public:
    void push(TelemetryEvent event);

    // Removes and returns up to `maxCount` queued events (oldest first),
    // for TelemetrySender to batch-upload. Returns fewer if the queue has
    // less than `maxCount` events.
    [[nodiscard]] std::vector<TelemetryEvent> drain(size_t maxCount);

    [[nodiscard]] size_t size() const;

private:
    mutable std::mutex mutex_;
    std::vector<TelemetryEvent> events_;
};

} // namespace engine::analytics
