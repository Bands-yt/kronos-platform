#pragma once

struct lua_State;

namespace engine::core {
class ECS;
}

namespace engine::studio {

// A small, ECS-only `world` Luau table (findByName/getPosition/
// setPosition/setColor) -- the same shape as core::ScriptWorldApi's
// `world` table (see ScriptWorldApi.hpp), but without the Physics/
// Animation calls that one also exposes, since neither Physics nor
// RuntimeAnimationPlayer exists in Studio (see StudioApp.hpp's class
// comment: "no Physics here"). Registers directly against whichever
// lua_State is passed (typically via core::Scripting::setBindingsHook())
// -- shared by studio::panels::DebugConsolePanel and
// studio::plugins::ScriptedPlugin, both of which run a real Luau VM
// against Studio's live ECS with nothing else available to script
// against, so this is the one implementation both attach, not two
// hand-copies quietly drifting apart from each other.
void registerStudioEcsBindings(lua_State* L, core::ECS& ecs);

} // namespace engine::studio
