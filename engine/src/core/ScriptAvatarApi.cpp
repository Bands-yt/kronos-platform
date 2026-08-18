#include "core/ScriptAvatarApi.hpp"

#include <lua.h>
#include <lualib.h>

#include "core/Application.hpp"
#include "core/ECS.hpp"

namespace engine::core {

namespace {
// Same closure-per-function/upvalue pattern ScriptWorldApi.cpp's own
// selfFromUpvalue() already establishes.
ScriptAvatarApi* selfFromUpvalue(lua_State* L) {
    return static_cast<ScriptAvatarApi*>(lua_tolightuserdata(L, lua_upvalueindex(1)));
}
} // namespace

ScriptAvatarApi::ScriptAvatarApi(Application& app) : app_(app) {}

int ScriptAvatarApi::luaSpawnPlayer(lua_State* L) {
    glm::vec3 position(luaL_checknumber(L, 1), luaL_checknumber(L, 2), luaL_checknumber(L, 3));
    Application& app = selfFromUpvalue(L)->app_;
    EntityId entity = app.respawnLocalPlayer(position);
    if (entity == kNullEntity) {
        lua_pushnil(L);
    } else {
        lua_pushnumber(L, static_cast<double>(static_cast<uint32_t>(entity)));
    }
    return 1;
}

int ScriptAvatarApi::luaPlayEmote(lua_State* L) {
    auto entity = static_cast<EntityId>(static_cast<uint32_t>(luaL_checknumber(L, 1)));
    const char* emoteId = luaL_checkstring(L, 2);
    bool looping = lua_isnone(L, 3) ? false : lua_toboolean(L, 3) != 0;
    Application& app = selfFromUpvalue(L)->app_;
    std::string error;
    bool played = app.tryPlayEmoteForEntity(entity, emoteId, looping, error);
    lua_pushboolean(L, played ? 1 : 0);
    if (!error.empty()) {
        lua_pushstring(L, error.c_str());
        return 2;
    }
    return 1;
}

void ScriptAvatarApi::registerInto(lua_State* L) {
    // Append onto the existing `world` table -- ScriptWorldApi::registerInto()
    // must have already run in this same lua_State (Application.cpp's own
    // setBindingsHook ordering guarantees this). A missing `world` global
    // here would mean this class was wired in the wrong order -- fail
    // loudly via lua_error rather than silently creating a second,
    // incomplete `world` table that would shadow the real one.
    lua_getglobal(L, "world");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        luaL_error(L, "ScriptAvatarApi::registerInto() requires ScriptWorldApi::registerInto() to run first");
        return;
    }
    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, &ScriptAvatarApi::luaSpawnPlayer, "spawnPlayer", 1);
    lua_setfield(L, -2, "spawnPlayer");
    lua_pop(L, 1); // world table

    lua_newtable(L);
    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, &ScriptAvatarApi::luaPlayEmote, "playEmote", 1);
    lua_setfield(L, -2, "playEmote");
    lua_setglobal(L, "avatar");
}

} // namespace engine::core
