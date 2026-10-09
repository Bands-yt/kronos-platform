#pragma once

#include <optional>
#include <string>
#include <vector>

#include "core/ECS.hpp"
#include "core/Scripting.hpp"

namespace engine::core::robloxScripts {

// A Script or LocalScript that follows Roblox's run rules: made by the
// importer, Instance.new or Clone (it carries a Disabled property). Kronos's
// own scripts keep Script::autoRun and their own VM.
[[nodiscard]] bool isRobloxScript(ECS& ecs, EntityId entity);

// Where a Roblox script would run right now, or nothing:
// - Script: under Workspace, ServerScriptService or a player's Backpack -> Server
// - LocalScript: under the local player (PlayerScripts, PlayerGui, Backpack),
//   its character, or ReplicatedFirst -> Client
// Only sides this process runs count (RunServiceState server/client).
[[nodiscard]] std::optional<RunContext> startContext(ECS& ecs, EntityId entity);

// Starts scripts that should run and stops destroyed, disabled or edited ones.
void tick(ECS& ecs, Scripting& scripting);

// Full names of the scripts started so far, in start order (for tools and tests).
[[nodiscard]] std::vector<std::string> startedNames(ECS& ecs);

// Forgets every started script (call when the Scripting instance is reset).
void reset(ECS& ecs);

} // namespace engine::core::robloxScripts
