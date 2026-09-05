#pragma once

#include <string>
#include <vector>

#include <memory>

#include "core/ECS.hpp"
#include "core/Scripting.hpp"
#include "core/ScriptMeshApi.hpp"
#include "studio/ScriptCinematicApi.hpp"
#include "studio/ScriptRenderApi.hpp"

namespace engine::core {
class Renderer;
}

namespace engine::studio::plugins {
class MovieModePlugin;
}

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
// and why that's a real constraint, not a missing feature), plus the
// real `mesh` table (core::ScriptMeshApi) -- unlike world's Physics/
// Animation gap, ScriptMeshApi needs nothing Studio lacks (only ECS +
// core::EditableMesh, both real here), so it's registered whole, not a
// cut-down version -- and the real `cinematic` table
// (studio::ScriptCinematicApi), sharing the SAME live cinematic::
// Sequence/CameraRail/ExportSettings studio::plugins::MovieModePlugin's
// own timeline UI edits, via that plugin's own sequence()/rail()/
// exportSettings() accessors (passed into initialize() below), and the
// real `render` table (studio::ScriptRenderApi) -- a real, deliberately
// narrow surface over the same core::Renderer post-FX tuning knobs
// studio::plugins::LightingToolsPlugin's own sliders drive, with no
// binding anywhere that returns a raw Vulkan handle -- see that class's
// own header comment for the full, stated security boundary. The
// Engine Log tab is a separate, real thing: every core::logDebug/Info/
// Warn/Error() call from anywhere in the process (not just this
// console's own scripts), filterable by level.
class DebugConsolePanel {
public:
    // `movieMode` must outlive this panel -- StudioApp owns both and
    // constructs/registers Movie Mode before calling this (see
    // StudioApp.cpp's own movieModePlugin_ capture, right after
    // registerPlugin()).
    // `renderer` must outlive this panel -- same "StudioApp owns both"
    // contract as `movieMode` (see this method's own existing comment).
    [[nodiscard]] bool initialize(core::ECS& ecs, plugins::MovieModePlugin& movieMode, core::Renderer& renderer);
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
    // Constructed lazily in initialize() (needs a live ECS& that doesn't
    // exist at panel-construction time), same reasoning as
    // core::Application::scriptWorldApi_'s own lazy construction.
    std::unique_ptr<core::ScriptMeshApi> scriptMeshApi_;
    // Same lazy-construction reasoning -- also needs `movieMode`'s own
    // sequence()/rail()/exportSettings() refs, which don't exist until
    // StudioApp has registered that plugin.
    std::unique_ptr<ScriptCinematicApi> scriptCinematicApi_;
    // Same lazy-construction reasoning -- needs a live core::Renderer&,
    // which doesn't exist until StudioApp's own Renderer is initialized.
    std::unique_ptr<ScriptRenderApi> scriptRenderApi_;

    std::vector<std::string> history_;
    std::string inputBuffer_;
    bool scrollToBottom_ = false;

    bool showDebugLogs_ = true;
    bool showInfoLogs_ = true;
    bool showWarnLogs_ = true;
    bool showErrorLogs_ = true;
};

} // namespace engine::studio::panels
