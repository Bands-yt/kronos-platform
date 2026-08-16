#pragma once

#include "core/Application.hpp"
#include "core/LocalGameDirectory.hpp"

namespace engine::runtime {

// Kronos ("Game Catalogue Overhaul", Phase 2): the real, honest way
// engine_runtime switches to a genuinely different local game -- not a
// simulation, an actual full teardown and rebuild of the live Physics
// world, Luau VM, and ECS, then a real core::SceneManager::loadScene()
// against the selected game's own project/scene data.
//
// Only meaningful for `game.manifest.launchKind == ProjectPath` --
// CliFlag-kind games (TNT Wars/Mining Sim/House Demo, still hardcoded
// C++ gameplay systems, not scene data) go through
// core::launchProcess() relaunching engine_runtime itself instead (see
// core/ProcessLaunch.hpp); loadGame() does not handle that case.
//
// Real ordering, and why: core::Physics::shutdown()+initialize() and
// core::Scripting::shutdown()+initialize() are already this codebase's
// proven "full reset" idiom (Scripting.hpp's own fireUnload() comment:
// studio::plugins::ScriptedPlugin::reload() "is exactly shutdown()+
// initialize() again") -- reused here, not reinvented. Physics and
// Scripting are torn down and rebuilt *before* core::SceneManager::
// loadScene() clears and repopulates the ECS, so no RigidBody component
// is ever left holding a stale joltBodyId against a torn-down
// PhysicsSystem -- this function runs synchronously within one call, with
// no render/physics tick interleaved, so there is no window where that
// could matter.
[[nodiscard]] bool loadGame(core::Application& app, const core::DiscoveredGame& game);

} // namespace engine::runtime
