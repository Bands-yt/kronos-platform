#pragma once

#include <string>
#include <vector>

struct lua_State;

namespace engine::net {
class NetworkSession;
struct ChatMessagePacket;
} // namespace engine::net

namespace engine::core {

class Scripting;

// The Luau `TextChatService` table.
//
// Named after Roblox's own service on purpose: this is the API a migrated
// script reaches for, and matching the name means a chat UI ported from
// Roblox needs its call sites rewritten, not its architecture.
//
// Surface:
//   TextChatService.SendAsync(message [, channel])   -- channel defaults to "General"
//   TextChatService.OnIncomingMessage(function(msg) end)
//
// The message passed to a handler is a table:
//   { senderId = <number>, channel = <string>, body = <string>, timestamp = <number> }
//
// SendAsync is NOT async in the Roblox sense of returning a promise --
// there are no promises in this VM. It sends and returns immediately;
// delivery is observed through OnIncomingMessage like every other client.
// The name is kept because that is what ported code calls, and silently
// having a *differently named* function would be worse than having one
// whose asynchrony is documented.
//
// Every send goes through NetworkSession, which means it goes through the
// same server-side moderation, rate limiting and mute/block filtering as
// a message typed into the HUD. There is deliberately no scripting path
// that bypasses that.
class ScriptChatApi {
public:
    ScriptChatApi(net::NetworkSession& session, Scripting& scripting);

    void registerInto(lua_State* L);

    // Called by the owner when a chat packet arrives, which fans it out to
    // every registered Luau handler.
    void dispatchIncoming(const net::ChatMessagePacket& packet);

private:
    struct RegisteredHandler {
        lua_State* owner = nullptr;
        int ref = -1;
    };

    static int luaSendAsync(lua_State* L);
    static int luaOnIncomingMessage(lua_State* L);

    net::NetworkSession& session_;
    Scripting& scripting_;
    std::vector<RegisteredHandler> handlers_;
};

} // namespace engine::core
