-- Session Debug -- a real network.* RPC debug logger.
--
-- What this ISN'T: a session-lifecycle watcher. events.onSessionJoin/
-- onSessionLeave/onPlayerJoin/onPlayerLeave exist in the `events` table
-- (it's the same table everywhere), but nothing in Studio ever calls
-- their fire-side (that wiring only exists in core::Application::
-- startNetworking(), which is engine_runtime-only) -- registering a
-- handler for them here would silently never fire, so this script
-- doesn't pretend otherwise.
--
-- What this IS: a real network.* round trip. Host or join a session
-- from Studio's own Network Overlay plugin first, then watch the Debug
-- Console -- every ~3s this fires a real "PluginDebugPing" and logs
-- whatever comes back. Before a session exists, fireServer() is a real,
-- honest no-op (see docs/LUA_API.md) -- you'll just see nothing happen,
-- not an error.

network.onServerEvent("PluginDebugPing", function(player, payload)
    print("Session Debug: PluginDebugPing from player " .. tostring(player) .. ": " .. tostring(payload.note))
end)

network.onClientEvent("PluginDebugPing", function(payload)
    print("Session Debug: PluginDebugPing broadcast received: " .. tostring(payload.note))
end)

local elapsedSinceLastPing = 0.0
local pingIntervalSeconds = 3.0
events.onUpdate(function(dt)
    elapsedSinceLastPing = elapsedSinceLastPing + dt
    if elapsedSinceLastPing < pingIntervalSeconds then return end
    elapsedSinceLastPing = 0.0
    network.fireServer("PluginDebugPing", {note = "ping from Session Debug plugin"})
end)

events.onUnload(function()
    print("Session Debug: unloaded")
end)
