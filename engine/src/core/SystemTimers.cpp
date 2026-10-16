#include "core/SystemTimers.hpp"

namespace engine::core {

SystemTimers& SystemTimers::instance() {
    static SystemTimers timers;
    return timers;
}

void SystemTimers::add(const char* name, double ms) {
    for (Pending& pending : pending_) {
        if (pending.name == name) {
            pending.ms += ms;
            return;
        }
    }
    pending_.push_back({name, ms});
}

void SystemTimers::endFrame() {
    for (Entry& entry : entries_) entry.lastFrameMs = 0.0;
    for (const Pending& pending : pending_) {
        Entry* found = nullptr;
        for (Entry& entry : entries_) {
            if (entry.name == pending.name) found = &entry;
        }
        if (found == nullptr) {
            entries_.push_back({pending.name, 0.0, pending.ms});
            found = &entries_.back();
        }
        found->lastFrameMs = pending.ms;
    }
    pending_.clear();
    for (Entry& entry : entries_) entry.smoothedMs += (entry.lastFrameMs - entry.smoothedMs) * 0.1;
}

void SystemTimers::reset() {
    entries_.clear();
    pending_.clear();
}

} // namespace engine::core
