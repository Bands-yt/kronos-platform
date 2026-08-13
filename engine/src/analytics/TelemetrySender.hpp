#pragma once

#include "analytics/TelemetryQueue.hpp"

namespace engine::analytics {

// Structural stub for §13's upload path: periodically drains
// TelemetryQueue and ships the batch to wherever the columnar event store
// (docs/ARCHITECTURE.md §3's analytics-pipeline stack entry) actually
// lives. No real network client exists here -- flush() logs what it would
// have sent and reports success, so callers (and a future real sender
// swapped in behind this same interface) can be exercised/tested without
// a backend to talk to yet.
class TelemetrySender {
public:
    explicit TelemetrySender(TelemetryQueue& queue) : queue_(queue) {}

    // Drains up to `batchSize` events and "sends" them. Returns the
    // number of events drained (0 if the queue was empty). A real
    // implementation compresses and POSTs the batch, retrying/re-queueing
    // on failure -- none of that exists yet, so this always reports the
    // batch as sent successfully once drained.
    size_t flush(size_t batchSize = 256);

private:
    TelemetryQueue& queue_;
};

} // namespace engine::analytics
