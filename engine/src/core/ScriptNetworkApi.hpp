#pragma once

#include <string>
#include <unordered_map>

#include "net/RemoteEvent.hpp"

struct lua_State;

namespace engine::net {
class NetworkSession;
}

namespace engine::core {

class Scripting;

// Kronos (Alpha Roadmap Phase 4, "Networking Upgrade"): the real Luau
// binding for net::RemoteEvent -- the C++-side schema/rate-limit
// enforcement primitive already existed (see RemoteEvent.hpp's own header
// comment) and net::NetworkSession already wires it into a real wire
// message pair (RemoteEventFire client->server, RemoteEventBroadcast
// server->client, see NetworkSession::remoteEvent()'s own comment), but
// nothing exposed either to a script before this -- "Reliable RPC system"
// / "Plugin access to networking API" from the roadmap's Networking
// Upgrade phase.
//
// Same deliberately-original, non-Roblox-parity shape ScriptWorldApi.hpp
// already established for `world` -- a flat `network` table, entities/
// events referenced by plain name/number, not an Instance-style RemoteEvent
// object with :FireServer()/:OnServerEvent methods:
//
//   network.fireServer("Purchase", {itemId = 42, quantity = 1})
//   network.onServerEvent("Purchase", function(player, payload) ... end)
//   network.fireAllClients("Announcement", {text = "Boss defeated!"})
//   network.onClientEvent("Announcement", function(payload) ... end)
//
// A payload is a real Lua table with string keys mapping to number/
// string/boolean values -- exactly net::RemoteEvent::Payload's own
// FieldValue variant; other value types (nested tables, functions) are
// silently dropped on the way out, matching that variant's real,
// documented scope (see RemoteEvent.hpp).
//
// registerInto() is what Scripting::setBindingsHook() attaches, one call
// per new script VM -- but `this` (and therefore serverHandlers_/
// clientHandlers_) is shared across every VM, the same "one C++ object,
// registered into many lua_States" shape ScriptWorldApi already uses.
//
// Known, accepted gap (matches Scripting::unload()'s own documented "KNOWN
// GAP"): a registered handler's lua_ref becomes stale if its owning
// script is unloaded without unregistering first. Not solved here since
// the base Scripting class hasn't solved the equivalent problem for its
// own onUpdate/onCollision/onInteract callbacks either -- tracked as one
// gap, not two independently-drifting ones.
class ScriptNetworkApi {
public:
    ScriptNetworkApi(net::NetworkSession& session, Scripting& scripting);

    void registerInto(lua_State* L);

private:
    struct RegisteredHandler {
        lua_State* owner = nullptr;
        int ref = -1;
    };

    static int luaFireServer(lua_State* L);
    static int luaFireAllClients(lua_State* L);
    static int luaOnServerEvent(lua_State* L);
    static int luaOnClientEvent(lua_State* L);

    static net::RemoteEvent::Payload payloadFromLuaTable(lua_State* L, int idx);
    static void pushPayloadAsLuaTable(lua_State* L, const net::RemoteEvent::Payload& payload);

    void invokeServerHandler(const RegisteredHandler& handler, net::PlayerId sender, const net::RemoteEvent::Payload& payload);
    void invokeClientHandler(const RegisteredHandler& handler, const net::RemoteEvent::Payload& payload);
    void dispatchClientEvent(const std::string& name, const net::RemoteEvent::Payload& payload);

    net::NetworkSession& session_;
    Scripting& scripting_;
    std::unordered_map<std::string, RegisteredHandler> serverHandlers_;
    std::unordered_map<std::string, RegisteredHandler> clientHandlers_;
};

} // namespace engine::core
