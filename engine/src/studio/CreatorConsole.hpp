#pragma once

#include <string>
#include <vector>

#include "core/ECS.hpp"

namespace engine::studio {

// Sprint 9 ("Creator Tools Phase 1") task category 5's "console for
// creators with warnings, errors, and tips" -- deliberately distinct
// from DebugConsolePanel (a Luau REPL + script log viewer, an entirely
// different tool for an entirely different audience/purpose): this is a
// real, rule-based scan of the *current scene's* authored state,
// surfacing genuine, actionable issues (a teleport pad with no link
// partner, a corrupted transform, a missing spawn point) rather than
// script output.
enum class ConsoleMessageSeverity { Tip, Warning, Error };

struct ConsoleMessage {
    ConsoleMessageSeverity severity = ConsoleMessageSeverity::Tip;
    std::string text;
};

// Pure ECS scan (no ImGui, headlessly testable) -- real-time, not cached:
// callers re-run this every frame the console panel is open, the same
// "live re-read, not a stale snapshot" convention StatsPanel/
// DiagnosticsPlugin already use. `hasTerrain` is a plain bool (not a
// Terrain* -- this module has no need to touch a live terrain instance
// itself, only to know whether one exists) so this stays fully
// headlessly testable without a live Vulkan device.
[[nodiscard]] std::vector<ConsoleMessage> scanSceneForMessages(core::ECS& ecs, bool hasTerrain);

} // namespace engine::studio
