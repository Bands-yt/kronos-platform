#pragma once

#include <string>
#include <vector>

#include "studio/VisualScript.hpp"

namespace engine::studio {

// First line of every generated script; marks Script sources the visual
// script editor owns and may overwrite.
inline constexpr const char* kVisualScriptSourceHeader =
    "-- Generated from a visual script. Edit the graph in Studio (Plugins > Visual Script); changes made here are replaced.";

[[nodiscard]] bool isGeneratedVisualScriptSource(const std::string& source);

struct VisualScriptCompileResult {
    bool success = false;
    std::string error;
    int errorNodeId = 0; // the node the error points at, or 0
    std::vector<std::string> warnings;
    std::string luau;
    std::string bytecode; // Luau bytecode, filled by compileVisualScript()
};

// Graph -> Luau source. Runs against the same API as hand-written scripts
// (world, events, task) and reads its own object from script.entity.
[[nodiscard]] VisualScriptCompileResult generateVisualScriptLuau(const VisualScriptGraph& graph);

// Graph -> Luau -> Luau bytecode, so the editor reports anything the VM
// would reject before the script ever runs.
[[nodiscard]] VisualScriptCompileResult compileVisualScript(const VisualScriptGraph& graph);

} // namespace engine::studio
