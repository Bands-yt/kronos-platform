#pragma once

#include "core/PerformanceDiagnostics.hpp"
#include "core/PerformanceMetrics.hpp"

namespace engine::studio::panels {

// Sprint 8 ("Performance Stats & Debug Tools") task category 1: a real,
// styled readout of a full core::PerformanceMetrics snapshot -- fps/frame
// time (color-coded by core::classifyFrameTimeSeverity()), draw calls,
// triangle count, GPU memory (color-coded by core::classifyMemorySeverity()),
// physics body counts, terrain chunk counts, and real process RSS/CPU
// (core::ProcessStatsSampler) -- plus three real mini-graphs (frame time,
// memory fraction, draw calls) backed by core::PerformanceHistory ring
// buffers this panel owns and pushes into every draw() call. Deliberately
// separate from the fuller Diagnostics panel (studio/plugins/DiagnosticsPlugin.hpp)
// -- this is still just a stats readout, not a place for per-entity/
// collapsible-section detail to grow into (that boundary is exactly what
// StatsPanel's original header comment already established; Diagnostics
// is the new, separate home for that instead of scope-creeping this one).
class StatsPanel {
public:
    void draw(const core::PerformanceMetrics& metrics);

private:
    core::PerformanceHistory frameTimeHistory_;
    core::PerformanceHistory memoryFractionHistory_;
    core::PerformanceHistory drawCallHistory_;
};

} // namespace engine::studio::panels
