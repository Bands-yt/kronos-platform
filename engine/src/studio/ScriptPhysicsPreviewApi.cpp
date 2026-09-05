#include "studio/ScriptPhysicsPreviewApi.hpp"

#include <lua.h>
#include <lualib.h>

#include "studio/plugins/PhysicsPreviewPlugin.hpp"

namespace engine::studio {

namespace {
ScriptPhysicsPreviewApi* selfFromUpvalue(lua_State* L) {
    return static_cast<ScriptPhysicsPreviewApi*>(lua_tolightuserdata(L, lua_upvalueindex(1)));
}
} // namespace

ScriptPhysicsPreviewApi::ScriptPhysicsPreviewApi(plugins::PhysicsPreviewPlugin& preview, core::ECS& ecs)
    : preview_(preview), ecs_(ecs) {}

int ScriptPhysicsPreviewApi::luaPause(lua_State* L) {
    selfFromUpvalue(L)->preview_.pause();
    return 0;
}

int ScriptPhysicsPreviewApi::luaResume(lua_State* L) {
    selfFromUpvalue(L)->preview_.resume();
    return 0;
}

int ScriptPhysicsPreviewApi::luaIsPaused(lua_State* L) {
    lua_pushboolean(L, selfFromUpvalue(L)->preview_.isPaused() ? 1 : 0);
    return 1;
}

int ScriptPhysicsPreviewApi::luaIsPlaying(lua_State* L) {
    lua_pushboolean(L, selfFromUpvalue(L)->preview_.isPlaying() ? 1 : 0);
    return 1;
}

int ScriptPhysicsPreviewApi::luaStep(lua_State* L) {
    float dt = static_cast<float>(luaL_checknumber(L, 1));
    ScriptPhysicsPreviewApi* self = selfFromUpvalue(L);
    lua_pushboolean(L, self->preview_.stepOnce(self->ecs_, dt) ? 1 : 0);
    return 1;
}

void ScriptPhysicsPreviewApi::registerInto(lua_State* L) {
    struct Entry {
        const char* name;
        lua_CFunction fn;
    };
    static constexpr Entry kEntries[] = {
        {"pause", &ScriptPhysicsPreviewApi::luaPause},   {"resume", &ScriptPhysicsPreviewApi::luaResume},
        {"isPaused", &ScriptPhysicsPreviewApi::luaIsPaused}, {"isPlaying", &ScriptPhysicsPreviewApi::luaIsPlaying},
        {"step", &ScriptPhysicsPreviewApi::luaStep},
    };

    lua_newtable(L);
    for (const Entry& entry : kEntries) {
        lua_pushlightuserdata(L, this);
        lua_pushcclosure(L, entry.fn, entry.name, 1);
        lua_setfield(L, -2, entry.name);
    }
    lua_setglobal(L, "physics");
}

} // namespace engine::studio
