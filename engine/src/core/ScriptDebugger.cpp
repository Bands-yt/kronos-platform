#include "core/ScriptDebugger.hpp"

namespace engine::core {

void ScriptDebugger::setBreakpoints(const std::string& chunk, const std::set<int>& lines) {
    auto it = breakpoints_.find(chunk);
    if (lines.empty()) {
        if (it == breakpoints_.end()) return;
        breakpoints_.erase(it);
    } else {
        if (it != breakpoints_.end() && it->second == lines) return;
        breakpoints_[chunk] = lines;
    }
    ++breakpointRevision_;
}

void ScriptDebugger::clearBreakpoints() {
    if (breakpoints_.empty()) return;
    breakpoints_.clear();
    ++breakpointRevision_;
}

void ScriptDebugger::resume(Action action) {
    if (!paused_) return;
    paused_ = false;
    resumeRequested_ = true;
    pendingAction_ = action;
}

} // namespace engine::core
