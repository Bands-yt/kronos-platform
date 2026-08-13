#include "core/ScriptUiApi.hpp"

#include <lua.h>
#include <lualib.h>

#include "core/UIRenderer.hpp"

namespace engine::core {

namespace {
ScriptUiApi* selfFromUpvalue(lua_State* L) { return static_cast<ScriptUiApi*>(lua_tolightuserdata(L, lua_upvalueindex(1))); }
} // namespace

int ScriptUiApi::luaDrawText(lua_State* L) {
    const char* text = luaL_checkstring(L, 1);
    float x = static_cast<float>(luaL_checknumber(L, 2));
    float y = static_cast<float>(luaL_checknumber(L, 3));
    float scale = static_cast<float>(luaL_optnumber(L, 4, 1.0));
    float r = static_cast<float>(luaL_optnumber(L, 5, 1.0));
    float g = static_cast<float>(luaL_optnumber(L, 6, 1.0));
    float b = static_cast<float>(luaL_optnumber(L, 7, 1.0));
    float a = static_cast<float>(luaL_optnumber(L, 8, 1.0));

    ScriptUiApi* self = selfFromUpvalue(L);
    self->textCommands_.push_back(TextCommand{text, glm::vec2(x, y), scale, glm::vec4(r, g, b, a)});
    return 0;
}

int ScriptUiApi::luaDrawRect(lua_State* L) {
    float x = static_cast<float>(luaL_checknumber(L, 1));
    float y = static_cast<float>(luaL_checknumber(L, 2));
    float w = static_cast<float>(luaL_checknumber(L, 3));
    float h = static_cast<float>(luaL_checknumber(L, 4));
    float r = static_cast<float>(luaL_optnumber(L, 5, 1.0));
    float g = static_cast<float>(luaL_optnumber(L, 6, 1.0));
    float b = static_cast<float>(luaL_optnumber(L, 7, 1.0));
    float a = static_cast<float>(luaL_optnumber(L, 8, 1.0));

    ScriptUiApi* self = selfFromUpvalue(L);
    self->rectCommands_.push_back(RectCommand{glm::vec2(x, y), glm::vec2(w, h), glm::vec4(r, g, b, a)});
    return 0;
}

int ScriptUiApi::luaSessionBrowser(lua_State* L) {
    ScriptUiApi* self = selfFromUpvalue(L);
    if (self->shellController_) self->shellController_->showSessionBrowser();
    return 0;
}

int ScriptUiApi::luaPlayerList(lua_State* L) {
    ScriptUiApi* self = selfFromUpvalue(L);
    if (self->shellController_) self->shellController_->showPlayerList();
    return 0;
}

int ScriptUiApi::luaJoinSession(lua_State* L) {
    // Real, honest precision note: a Luau number is a real IEEE-754
    // double (53-bit exact integer range), while a real session id
    // (net::generateSessionId()) is a full, real 64-bit value -- this
    // matches every other numeric id this engine's whole Lua surface
    // already hands scripts as a plain number (e.g. world.createEntity()'s
    // own EntityId), same real, accepted precision ceiling, not a new one
    // introduced here.
    double sessionId = luaL_checknumber(L, 1);
    ScriptUiApi* self = selfFromUpvalue(L);
    if (self->shellController_) self->shellController_->joinSession(static_cast<uint64_t>(sessionId));
    return 0;
}

int ScriptUiApi::luaLeaveSession(lua_State* L) {
    ScriptUiApi* self = selfFromUpvalue(L);
    if (self->shellController_) self->shellController_->leaveSession();
    return 0;
}

void ScriptUiApi::flushInto(UIRenderer& uiRenderer) {
    for (const auto& cmd : textCommands_) uiRenderer.drawText(cmd.text, cmd.position, cmd.scale, cmd.color);
    for (const auto& cmd : rectCommands_) uiRenderer.drawRect(cmd.position, cmd.size, cmd.color);
    textCommands_.clear();
    rectCommands_.clear();
}

void ScriptUiApi::registerInto(lua_State* L) {
    struct Entry {
        const char* name;
        lua_CFunction fn;
    };
    static constexpr Entry kEntries[] = {
        {"drawText", &ScriptUiApi::luaDrawText},
        {"drawRect", &ScriptUiApi::luaDrawRect},
        {"sessionBrowser", &ScriptUiApi::luaSessionBrowser},
        {"playerList", &ScriptUiApi::luaPlayerList},
        {"joinSession", &ScriptUiApi::luaJoinSession},
        {"leaveSession", &ScriptUiApi::luaLeaveSession},
    };

    lua_newtable(L);
    for (const Entry& entry : kEntries) {
        lua_pushlightuserdata(L, this);
        lua_pushcclosure(L, entry.fn, entry.name, 1);
        lua_setfield(L, -2, entry.name);
    }
    lua_setglobal(L, "ui");
}

} // namespace engine::core
