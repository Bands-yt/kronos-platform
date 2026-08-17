#pragma once

#include "core/PerformanceDiagnostics.hpp"
#include "core/PerformanceMetrics.hpp"

namespace engine::core {
class Scripting;
}

namespace engine::studio::panels {

// Kronos ("Developer Velocity Sprint" -- "Real-Time Visual Performance
// Profiler"): a real, F3-toggleable floating overlay -- deliberately
// separate from the always-docked StatsPanel (studio/panels/StatsPanel.hpp),
// which stays exactly as it was (an always-visible dock panel, not an
// optional overlay); this is a new, additive surface, not a repurposing
// of that one. Reuses the same real core::PerformanceHistory ring-buffer
// + ImGui::PlotLines() convention StatsPanel already established for its
// own frame-time graph, plus real draw call / GPU memory numbers already
// in core::PerformanceMetrics, plus a real Lua memory reading (new this
// pass -- see core::Scripting::totalUsedMemoryBytes()).
class PerformanceOverlayPanel {
public:
    void toggle() { open_ = !open_; }
    [[nodiscard]] bool isOpen() const { return open_; }
    void setOpen(bool open) { open_ = open; }

    // `scripting` is nullable -- null whenever no live core::Scripting
    // session exists (Studio outside Play mode; see
    // studio::plugins::PhysicsPreviewPlugin's own class comment on why
    // Studio otherwise runs no scripting at all). A real, honest "N/A"
    // is shown rather than a fabricated zero when that's the case.
    void draw(const core::PerformanceMetrics& metrics, const core::Scripting* scripting);

private:
    bool open_ = false;
    core::PerformanceHistory frameTimeHistory_;
};

} // namespace engine::studio::panels
