#pragma once

#include <string>
#include <vector>

#include "core/ECS.hpp"
#include "core/Scripting.hpp"

namespace engine::studio::panels {

// A real Luau REPL-style console docked in Studio -- type a snippet, hit
// Run, see what it printed. Owns its own core::Scripting instance (the
// real Luau VM, same sandboxing/memory/watchdog budgets as
// engine_runtime's, not a mock interpreter) rather than sharing one with
// a future Play Solo session: StudioApp explicitly runs no
// Physics/Scripting/Audio of its own today (see StudioApp.hpp's class
// comment), and this console's whole point -- being usable to poke at
// the bring-up scene right now, independent of whether Play Solo exists
// yet -- would break if it had to wait for that session to be running.
//
// Kronos (Alpha Roadmap Phase 6, "Tooling Layer" -- "Console + logs"):
// also the real home for a live view of core::Logger's ring buffer
// (Logger.hpp's own header comment named this exact panel as where that
// would land: "a bounded thread-safe ring buffer a future Studio
// 'Console' panel can read back from directly instead of re-parsing
// stdout"). Bindings exposed on the REPL tab are print/engine.log/task.*
// (via core::Scripting itself) plus a small ECS-only `world` table
// (studio::registerStudioEcsBindings(), StudioEcsScriptApi.hpp -- see its
// header for why this is a smaller table than core::ScriptWorldApi's,
// and why that's a real constraint, not a missing feature). The Engine
// Log tab is a separate, real thing: every core::logDebug/Info/Warn/
// Error() call from anywhere in the process (not just this console's own
// scripts), filterable by level.
class DebugConsolePanel {
public:
    [[nodiscard]] bool initialize(core::ECS& ecs);
    void shutdown();

    // Call once per Studio frame, unconditionally (same as any other
    // panel's per-frame hook) -- drives task.wait/spawn/defer scheduling
    // and events.onUpdate, exactly like Scripting::tick() does for
    // engine_runtime's GameLoop.
    void tick(float dt);

    void draw();

private:
    void appendLine(const std::string& line);
    void drawReplTab();
    void drawEngineLogTab();

    core::Scripting scripting_;
    core::ECS* ecs_ = nullptr;

    std::vector<std::string> history_;
    std::string inputBuffer_;
    bool scrollToBottom_ = false;

    bool showDebugLogs_ = true;
    bool showInfoLogs_ = true;
    bool showWarnLogs_ = true;
    bool showErrorLogs_ = true;
};

} // namespace engine::studio::panels
