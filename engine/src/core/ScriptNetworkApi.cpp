#include "core/ScriptNetworkApi.hpp"

#include <lua.h>
#include <lualib.h>

#include <cstdio>

#include "core/Scripting.hpp"
#include "net/NetworkSession.hpp"

namespace engine::core {

namespace {
ScriptNetworkApi* selfFromUpvalue(lua_State* L) {
    return static_cast<ScriptNetworkApi*>(lua_tolightuserdata(L, lua_upvalueindex(1)));
}
} // namespace

ScriptNetworkApi::ScriptNetworkApi(net::NetworkSession& session, Scripting& scripting)
    : session_(session), scripting_(scripting) {
    session_.setOnClientEventReceived(
        [this](const std::string& name, const net::RemoteEvent::Payload& payload) { dispatchClientEvent(name, payload); });
}

net::RemoteEvent::Payload ScriptNetworkApi::payloadFromLuaTable(lua_State* L, int idx) {
    net::RemoteEvent::Payload payload;
    idx = lua_absindex(L, idx);
    lua_pushnil(L);
    while (lua_next(L, idx) != 0) {
        // stack: ... key value
        if (lua_type(L, -2) == LUA_TSTRING) {
            std::string key = lua_tostring(L, -2);
            int valueType = lua_type(L, -1);
            if (valueType == LUA_TNUMBER) {
                payload[key] = static_cast<double>(lua_tonumber(L, -1));
            } else if (valueType == LUA_TSTRING) {
                payload[key] = std::string(lua_tostring(L, -1));
            } else if (valueType == LUA_TBOOLEAN) {
                payload[key] = lua_toboolean(L, -1) != 0;
            }
            // Other value types (nested tables, functions, ...) are real,
            // silently dropped -- see this class's own header comment on
            // RemoteEvent::Payload's real, documented scope.
        }
        lua_pop(L, 1); // pop value, keep key on the stack for the next lua_next()
    }
    return payload;
}

void ScriptNetworkApi::pushPayloadAsLuaTable(lua_State* L, const net::RemoteEvent::Payload& payload) {
    lua_newtable(L);
    for (const auto& [key, value] : payload) {
        if (std::holds_alternative<double>(value)) {
            lua_pushnumber(L, std::get<double>(value));
        } else if (std::holds_alternative<std::string>(value)) {
            lua_pushstring(L, std::get<std::string>(value).c_str());
        } else {
            lua_pushboolean(L, std::get<bool>(value) ? 1 : 0);
        }
        lua_setfield(L, -2, key.c_str());
    }
}

void ScriptNetworkApi::invokeServerHandler(const RegisteredHandler& handler, net::PlayerId sender,
                                            const net::RemoteEvent::Payload& payload) {
    lua_State* L = handler.owner;
    scripting_.refreshWatchdogDeadline(L);
    lua_getref(L, handler.ref);
    lua_pushnumber(L, static_cast<double>(sender));
    pushPayloadAsLuaTable(L, payload);
    int status = lua_pcall(L, 2, 0, 0);
    if (status != LUA_OK) {
        std::fprintf(stderr, "[luau] network.onServerEvent callback error: %s\n", lua_tostring(L, -1));
        lua_pop(L, 1);
    }
}

void ScriptNetworkApi::invokeClientHandler(const RegisteredHandler& handler, const net::RemoteEvent::Payload& payload) {
    lua_State* L = handler.owner;
    scripting_.refreshWatchdogDeadline(L);
    lua_getref(L, handler.ref);
    pushPayloadAsLuaTable(L, payload);
    int status = lua_pcall(L, 1, 0, 0);
    if (status != LUA_OK) {
        std::fprintf(stderr, "[luau] network.onClientEvent callback error: %s\n", lua_tostring(L, -1));
        lua_pop(L, 1);
    }
}

void ScriptNetworkApi::dispatchClientEvent(const std::string& name, const net::RemoteEvent::Payload& payload) {
    auto it = clientHandlers_.find(name);
    if (it == clientHandlers_.end()) return;
    invokeClientHandler(it->second, payload);
}

int ScriptNetworkApi::luaFireServer(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    net::RemoteEvent::Payload payload = lua_istable(L, 2) ? payloadFromLuaTable(L, 2) : net::RemoteEvent::Payload{};
    selfFromUpvalue(L)->session_.fireServerEvent(name, payload);
    return 0;
}

int ScriptNetworkApi::luaFireAllClients(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    net::RemoteEvent::Payload payload = lua_istable(L, 2) ? payloadFromLuaTable(L, 2) : net::RemoteEvent::Payload{};
    selfFromUpvalue(L)->session_.fireAllClientsEvent(name, payload);
    return 0;
}

int ScriptNetworkApi::luaOnServerEvent(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    ScriptNetworkApi* self = selfFromUpvalue(L);

    lua_State* owner = lua_mainthread(L); // registration can happen from a spawned coroutine, not just top level
    lua_pushvalue(L, 2);
    int ref = lua_ref(L, -1);
    lua_pop(L, 1);

    std::string eventName = name;
    self->serverHandlers_[eventName] = RegisteredHandler{owner, ref};

    // Real-wired into the actual RemoteEvent's own single handler slot --
    // a second network.onServerEvent() call for the same name replaces
    // both the map entry above and this real handler, matching
    // RemoteEvent::setServerHandler()'s own single-handler contract.
    self->session_.remoteEvent(eventName).setServerHandler(
        [self, eventName](net::PlayerId sender, const net::RemoteEvent::Payload& payload) {
            auto it = self->serverHandlers_.find(eventName);
            if (it != self->serverHandlers_.end()) self->invokeServerHandler(it->second, sender, payload);
        });
    return 0;
}

int ScriptNetworkApi::luaOnClientEvent(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    ScriptNetworkApi* self = selfFromUpvalue(L);

    lua_State* owner = lua_mainthread(L);
    lua_pushvalue(L, 2);
    int ref = lua_ref(L, -1);
    lua_pop(L, 1);

    self->clientHandlers_[name] = RegisteredHandler{owner, ref};
    return 0;
}

void ScriptNetworkApi::registerInto(lua_State* L) {
    struct Entry {
        const char* name;
        lua_CFunction fn;
    };
    static constexpr Entry kEntries[] = {
        {"fireServer", &ScriptNetworkApi::luaFireServer},
        {"fireAllClients", &ScriptNetworkApi::luaFireAllClients},
        {"onServerEvent", &ScriptNetworkApi::luaOnServerEvent},
        {"onClientEvent", &ScriptNetworkApi::luaOnClientEvent},
    };

    lua_newtable(L);
    for (const Entry& entry : kEntries) {
        lua_pushlightuserdata(L, this);
        lua_pushcclosure(L, entry.fn, entry.name, 1);
        lua_setfield(L, -2, entry.name);
    }
    lua_setglobal(L, "network");
}

} // namespace engine::core
