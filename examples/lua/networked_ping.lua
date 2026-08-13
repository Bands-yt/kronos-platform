-- Kronos Lua Example: Networked Ping
--
-- A real client/server round trip using this engine's real event-name
-- RPC (see ../../docs/LUA_API.md's `network` section and
-- ../../docs/NETWORKING_UPGRADE.md for the real wire protocol
-- underneath). Load the SAME script on both a server Scripting instance
-- and a client Scripting instance -- each half is a real, honest no-op
-- in the wrong context, so one file works for both sides, matching how
-- a real Kronos project shares gameplay scripts between client and
-- server builds.

-- Client side: press-to-ping. Call pingServer() from your own input
-- handling code, or just watch it fire once on load for this example.
local function pingServer()
    network.fireServer("Ping", {sentAt = 0})
end

-- Server side: reply to a ping by broadcasting a pong to every client.
network.onServerEvent("Ping", function(player, payload)
    print("Ping example: received Ping from player " .. tostring(player))
    network.fireAllClients("Pong", {receivedFrom = player})
end)

-- Client side: react to the server's broadcast.
network.onClientEvent("Pong", function(payload)
    print("Ping example: received Pong, server saw player " .. tostring(payload.receivedFrom))
end)

pingServer()
