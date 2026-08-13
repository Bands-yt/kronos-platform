#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <variant>

namespace engine::analytics {

using TelemetryValue = std::variant<double, std::string, bool, int64_t>;

// One structured analytics event. Device breakdown, performance metrics,
// crash context, input-method usage, and retention/monetization signals
// (docs/ARCHITECTURE.md §13) all flow through this same shape rather than
// a separate event type per category, matching §13's "one lightweight
// telemetry SDK ... not a parallel system [per category]."
struct TelemetryEvent {
    std::string name; // e.g. "frame_time", "purchase_completed", "session_start"
    std::chrono::system_clock::time_point timestamp = std::chrono::system_clock::now();
    std::unordered_map<std::string, TelemetryValue> properties;
};

} // namespace engine::analytics
