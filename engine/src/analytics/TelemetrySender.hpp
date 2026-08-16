#pragma once

#include <string>

#include "analytics/TelemetryQueue.hpp"

namespace engine::analytics {

// §13's real upload path, minus the actual network backend: periodically
// drains TelemetryQueue and appends each real event to a real local log
// file (Kronos "Game Catalogue Overhaul", Phase 6 -- "logging for
// ML": "from release onward, log gameplay + retention data" needed a
// genuinely real, persisted sink, not a stdout message claiming a batch
// "would" be uploaded). What's still NOT real: the actual network
// client/backend upload (docs/ARCHITECTURE.md §3's columnar event
// store) -- `logPath_` is the real, honest local Alpha substitute for
// that, same "local only for alpha" framing net::SessionHistory/
// net::GamePlayLog already establish elsewhere in this codebase. A real
// backend sender swapped in behind this same flush()/drain() interface
// later would read from this same real log as its own retry queue.
class TelemetrySender {
public:
    // `logPath` is where flush() appends real, structured event lines --
    // point this somewhere real logs already live, same convention
    // analytics::CrashReporter::install()'s own `crashLogPath` parameter
    // already establishes.
    explicit TelemetrySender(TelemetryQueue& queue, std::string logPath = "telemetry.log")
        : queue_(queue), logPath_(std::move(logPath)) {}

    // Drains up to `batchSize` events and appends each one, one real
    // line per event, to `logPath_`. Returns the number of events
    // drained (0 if the queue was empty). Real, honest degradation if
    // the log file can't be opened for append: the batch is still
    // drained (so the queue doesn't grow unbounded) but the events are
    // lost and a real stderr message says so -- matches this codebase's
    // "fail soft, don't silently pretend it worked" convention
    // throughout (e.g. SceneManager's own mesh-regeneration-failure
    // path).
    size_t flush(size_t batchSize = 256);

private:
    TelemetryQueue& queue_;
    std::string logPath_;
};

} // namespace engine::analytics
