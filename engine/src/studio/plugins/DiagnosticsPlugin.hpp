#pragma once

#include <string>
#include <vector>

#include "core/PerformanceDiagnostics.hpp"
#include "core/PerformanceMetrics.hpp"
#include "core/Profiler.hpp"
#include "studio/IStudioPlugin.hpp"
#include "studio/Notification.hpp"

namespace engine::studio::plugins {

// Sprint 8 ("Performance Stats & Debug Tools") task category 4: a real
// Diagnostics panel with collapsible sections over live engine state --
// Renderer/Physics/Terrain/Process metrics (the same composed
// core::PerformanceMetrics snapshot StatsPanel reads, see StudioApp's own
// per-frame composition), the Profiler's real event log plus Performance
// Recording controls, the Notification queue's live count, a real ECS
// entity count, and, task category 4's "live updates when selecting
// objects": a Selected Entity section that shows the primary selection's
// real component checklist + its real EntityCategory (studio::classifyEntity(),
// Sprint 7) every frame, updating the instant Explorer's selection
// changes -- not a static snapshot taken once.
//
// Deliberately separate from StatsPanel (still just fps/frame-
// time/draw-calls/memory, see its own header comment) -- this is the
// "fuller debug console"-adjacent home for detail that would otherwise
// have scope-crept StatsPanel into something it was never meant to be.
class DiagnosticsPlugin final : public IStudioPlugin {
public:
    // References into StudioApp-owned state, the same "plugin holds a
    // reference to something StudioApp already owns, bound once at
    // construction" pattern PluginBrowserPlugin's own PluginManager&
    // reference already established. `metrics` is StudioApp's own
    // per-frame-reassigned PerformanceMetrics member -- a stored
    // reference to it keeps reflecting the latest frame's data since
    // StudioApp assigns *through* it (composePerformanceMetrics()'s
    // result), never rebinds it.
    DiagnosticsPlugin(core::Profiler& profiler, const core::PerformanceMetrics& metrics,
                       const NotificationCenter& notifications);

    [[nodiscard]] const char* name() const override { return "Diagnostics"; }
    [[nodiscard]] const char* category() const override { return "Diagnostics"; }

    void drawPanel(core::ECS& ecs, core::EntityId selected, const std::vector<core::EntityId>& selectedEntities) override;

private:
    void drawMetricsSection() const;
    void drawProfilerSection();
    void drawSelectedEntitySection(core::ECS& ecs, core::EntityId selected) const;

    core::Profiler* profiler_;
    const core::PerformanceMetrics* metrics_;
    const NotificationCenter* notifications_;

    char recordingPathBuffer_[256] = "performance_recording.json";
    std::string lastRecordingStatus_;
};

} // namespace engine::studio::plugins
