#include "analytics/TelemetrySender.hpp"

#include <cstdio>

namespace engine::analytics {

size_t TelemetrySender::flush(size_t batchSize) {
    std::vector<TelemetryEvent> batch = queue_.drain(batchSize);
    if (batch.empty()) return 0;

    // TODO: compress + POST to the real analytics backend (§3), with
    // retry/re-queue on failure. For now, just prove the batch was
    // actually drained.
    std::fprintf(stdout, "TelemetrySender: flush() would upload %zu event(s) (no backend wired yet)\n", batch.size());
    return batch.size();
}

} // namespace engine::analytics
