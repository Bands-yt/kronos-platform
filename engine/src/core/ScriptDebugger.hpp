#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

struct lua_State;

namespace engine::core {

struct ScriptDebugVariable {
    std::string name;
    std::string type;
    std::string value;
    bool upvalue = false;
};

struct ScriptDebugFrame {
    std::string function;
    std::string chunk;
    int line = 0;
    std::vector<ScriptDebugVariable> variables;
};

struct ScriptPauseState {
    std::string chunk;
    int line = 0;
    std::string reason;
    std::vector<ScriptDebugFrame> frames;
};

// Breakpoint/step debugger for core::Scripting. Breakpoints are keyed by
// chunk name (the script entity's name) with 1-based lines. Scripting does
// the VM work; this holds what the UI reads and writes.
class ScriptDebugger {
public:
    enum class Action { Continue, StepInto, StepOver, StepOut };

    void setBreakpoints(const std::string& chunk, const std::set<int>& lines);
    void clearBreakpoints();
    [[nodiscard]] const std::map<std::string, std::set<int>>& breakpoints() const { return breakpoints_; }
    [[nodiscard]] uint64_t breakpointRevision() const { return breakpointRevision_; }

    // Breaks at the next statement any script runs.
    void requestPause() { pauseRequested_ = true; }
    [[nodiscard]] bool pauseRequested() const { return pauseRequested_; }

    void resume(Action action);
    [[nodiscard]] bool paused() const { return paused_; }
    [[nodiscard]] const ScriptPauseState& pauseState() const { return pauseState_; }
    // Bumped on every pause, so a UI can react once per stop.
    [[nodiscard]] uint64_t pauseSerial() const { return pauseSerial_; }

    // A message for breakpoints that could not stop (e.g. inside a C call).
    [[nodiscard]] const std::string& lastNotice() const { return lastNotice_; }

private:
    friend class Scripting;

    std::map<std::string, std::set<int>> breakpoints_;
    uint64_t breakpointRevision_ = 0;
    bool pauseRequested_ = false;

    bool paused_ = false;
    bool resumeRequested_ = false;
    Action pendingAction_ = Action::Continue;
    ScriptPauseState pauseState_;
    uint64_t pauseSerial_ = 0;
    int pausedDepth_ = 0;

    Action stepAction_ = Action::Continue;
    int stepDepth_ = 0;
    int stepLine_ = 0;
    std::string lastNotice_;
};

} // namespace engine::core
