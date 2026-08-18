#pragma once

struct lua_State;

namespace engine::core {

class Application;

// Kronos ("Kronos Scripting Environment" -- "Immediate Gaps for Launch"):
// the real Luau binding for the two player/avatar-lifecycle gaps found
// missing from ScriptWorldApi's own entity-level primitives --
// `world.spawnPlayer` and `avatar.playEmote`. Deliberately a separate
// class from ScriptWorldApi (which holds ECS&/Physics&/
// RuntimeAnimationPlayer& directly, by design decoupled from Application
// -- see ScriptWorldApi.hpp's own header comment) rather than growing
// ScriptWorldApi's own scope: both functions here need real
// Application-level orchestration (avatarController_'s spawned/not-
// spawned state, the late-bound AnimationDatabase, characterController_'s
// own entity) that only Application itself owns.
//
// `world.spawnPlayer(x, y, z)` is added onto the *same* global `world`
// table ScriptWorldApi::registerInto() already created (this class's own
// registerInto() must run after ScriptWorldApi's -- see
// Scripting::setBindingsHook()'s wiring in Application.cpp), not a
// second `world` table -- the user's own request named this function as
// living on `world`, matching ScriptWorldApi's existing entity-lifecycle
// functions (createEntity/destroy) even though the real work happens
// through Application.
//
// `avatar.playEmote(entityId, emoteName, [looping])` is a real, new
// `avatar` table -- the first one in this codebase (Scripting.hpp's own
// header comment already flags that no `player`/`avatar` table exists
// yet). Only the local player has a live AvatarController today (see
// Application::tryPlayEmoteForEntity()'s own .cpp comment) -- `entityId`
// is still a real, checked parameter (not silently ignored) so a script
// gets an honest `false`, not a false "success," for any other entity.
//
// Deliberately does NOT take `playerId` the way the user's original
// request phrased `world.spawnPlayer(playerId, x, y, z)` -- no per-
// player targeting concept exists anywhere in this codebase (only ever
// "the" local player, see Application::respawnLocalPlayer()'s own
// comment); inventing one to control other, non-local players from
// script would be a real, separate, much larger networking-authority
// feature, not a same-shape binding addition.
class ScriptAvatarApi {
public:
    explicit ScriptAvatarApi(Application& app);

    void registerInto(lua_State* L);

private:
    static int luaSpawnPlayer(lua_State* L);
    static int luaPlayEmote(lua_State* L);

    Application& app_;
};

} // namespace engine::core
