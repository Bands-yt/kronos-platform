#include "analytics/TelemetrySender.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>

namespace engine::analytics {

namespace {

// Real, simple, write-only serialization -- `name|unix_seconds|k=v;k=v`
// per line. No reader/loader exists for this format in this pass (see
// TelemetrySender.hpp's own class comment on what's still not real: the
// actual network backend upload); a string property containing '|'/';'/
// '=' wouldn't round-trip through a future reader, a real, stated
// limitation rather than a silently-assumed-safe one, acceptable since
// nothing parses this file back yet.
std::string serializeEvent(const TelemetryEvent& event) {
    std::ostringstream out;
    int64_t unixSeconds =
        std::chrono::duration_cast<std::chrono::seconds>(event.timestamp.time_since_epoch()).count();
    out << event.name << '|' << unixSeconds << '|';

    bool first = true;
    for (const auto& [key, value] : event.properties) {
        if (!first) out << ';';
        first = false;
        out << key << '=';
        if (const auto* d = std::get_if<double>(&value)) {
            out << *d;
        } else if (const auto* s = std::get_if<std::string>(&value)) {
            out << *s;
        } else if (const auto* b = std::get_if<bool>(&value)) {
            out << (*b ? "true" : "false");
        } else if (const auto* i = std::get_if<int64_t>(&value)) {
            out << *i;
        }
    }
    return out.str();
}

} // namespace

size_t TelemetrySender::flush(size_t batchSize) {
    std::vector<TelemetryEvent> batch = queue_.drain(batchSize);
    if (batch.empty()) return 0;

    std::ofstream out(logPath_, std::ios::app);
    if (!out.is_open()) {
        std::fprintf(stderr, "TelemetrySender: flush() drained %zu event(s) but couldn't open \"%s\" for append -- lost\n",
                     batch.size(), logPath_.c_str());
        return batch.size();
    }
    for (const auto& event : batch) out << serializeEvent(event) << "\n";

    return batch.size();
}

} // namespace engine::analytics
