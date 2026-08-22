#include "core/ScriptChatApi.hpp"

#include <lua.h>
#include <lualib.h>

#include <cstdio>

#include "core/Scripting.hpp"
#include "net/ChatProtocol.hpp"
#include "net/NetworkSession.hpp"

namespace engine::core {

namespace {

ScriptChatApi* selfFromUpvalue(lua_State* L) {
    return static_cast<ScriptChatApi*>(lua_tolightuserdata(L, lua_upvalueindex(1)));
}

// Channels are named in Luau and numeric on the wire. Scripts should not
// be writing magic numbers, and the wire should not be carrying spoofable
// strings -- this is the one place the two representations meet.
bool channelFromName(const char* name, net::ChatChannel& out) {
    if (name == nullptr) return false;
    const std::string text = name;
    if (text == "General") { out = net::ChatChannel::General; return true; }
    if (text == "Team") { out = net::ChatChannel::Team; return true; }
    if (text == "Whisper") { out = net::ChatChannel::Whisper; return true; }
    // "System" is deliberately absent: it is server-authored, and a script
    // that could send on it could forge official notices.
    return false;
}

} // namespace

ScriptChatApi::ScriptChatApi(net::NetworkSession& session, Scripting& scripting)
    : session_(session), scripting_(scripting) {
    session_.setOnChatPacketReceived([this](const net::ChatMessagePacket& packet) { dispatchIncoming(packet); });
}

int ScriptChatApi::luaSendAsync(lua_State* L) {
    const char* message = luaL_checkstring(L, 1);
    ScriptChatApi* self = selfFromUpvalue(L);

    net::ChatChannel channel = net::ChatChannel::General;
    if (lua_gettop(L) >= 2 && !lua_isnil(L, 2)) {
        const char* channelName = luaL_checkstring(L, 2);
        if (!channelFromName(channelName, channel)) {
            luaL_error(L, "TextChatService.SendAsync: unknown or non-sendable channel \"%s\" "
                          "(use \"General\", \"Team\" or \"Whisper\")",
                        channelName != nullptr ? channelName : "");
            return 0;
        }
    }

    const std::string body = message != nullptr ? message : "";
    if (body.empty()) {
        luaL_error(L, "TextChatService.SendAsync: message is empty");
        return 0;
    }
    if (body.size() > net::ChatMessagePacket::kMaxBodyBytes) {
        // Refused rather than truncated: silently cutting a message in
        // half is worse than telling the author their message is too long.
        luaL_error(L, "TextChatService.SendAsync: message is %d bytes, the limit is %d", static_cast<int>(body.size()),
                    static_cast<int>(net::ChatMessagePacket::kMaxBodyBytes));
        return 0;
    }

    self->session_.sendChatMessageOn(channel, body);
    return 0;
}

int ScriptChatApi::luaOnIncomingMessage(lua_State* L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);
    ScriptChatApi* self = selfFromUpvalue(L);

    // Registration can happen from a spawned coroutine, not just top
    // level -- same reasoning as ScriptNetworkApi::luaOnServerEvent.
    lua_State* owner = lua_mainthread(L);
    lua_pushvalue(L, 1);
    const int ref = lua_ref(L, -1);
    lua_pop(L, 1);

    self->handlers_.push_back(RegisteredHandler{owner, ref});
    return 0;
}

void ScriptChatApi::dispatchIncoming(const net::ChatMessagePacket& packet) {
    for (const RegisteredHandler& handler : handlers_) {
        if (handler.owner == nullptr || handler.ref < 0) continue;
        lua_State* L = handler.owner;

        // Refreshed before entering Lua, exactly as ScriptNetworkApi does:
        // an incoming message arrives at an arbitrary point in the frame,
        // and without this the handler inherits whatever remains of some
        // other script's execution budget and is killed mid-callback.
        scripting_.refreshWatchdogDeadline(L);
        lua_getref(L, handler.ref);

        lua_newtable(L);
        lua_pushnumber(L, static_cast<double>(packet.senderId));
        lua_setfield(L, -2, "senderId");
        lua_pushstring(L, net::chatChannelName(static_cast<net::ChatChannel>(packet.channelId)));
        lua_setfield(L, -2, "channel");
        lua_pushstring(L, packet.body.c_str());
        lua_setfield(L, -2, "body");
        lua_pushnumber(L, static_cast<double>(packet.timestampMillis));
        lua_setfield(L, -2, "timestamp");

        const int status = lua_pcall(L, 1, 0, 0);
        if (status != LUA_OK) {
            // Reported, not rethrown: one script's broken chat handler
            // must not stop the message reaching every other subscriber.
            std::fprintf(stderr, "[luau] TextChatService.OnIncomingMessage callback error: %s\n", lua_tostring(L, -1));
            lua_pop(L, 1);
        }
    }
}

void ScriptChatApi::registerInto(lua_State* L) {
    struct Entry {
        const char* name;
        lua_CFunction fn;
    };
    static constexpr Entry kEntries[] = {
        {"SendAsync", &ScriptChatApi::luaSendAsync},
        {"OnIncomingMessage", &ScriptChatApi::luaOnIncomingMessage},
    };

    lua_newtable(L);
    for (const Entry& entry : kEntries) {
        lua_pushlightuserdata(L, this);
        lua_pushcclosure(L, entry.fn, entry.name, 1);
        lua_setfield(L, -2, entry.name);
    }
    lua_setglobal(L, "TextChatService");
}

} // namespace engine::core
