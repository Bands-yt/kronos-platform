-- A Roblox script already ported to Kronos, kept beside the raw sample so
-- the difference is concrete. Everything below uses APIs Kronos really
-- provides -- see docs/LUA_API.md -- so an import scan reports no
-- migration warnings for it.

local MathUtil = require("ReplicatedStorage/MathUtil")

events.onPlayerJoin(function(playerId, displayName)
    print("welcome " .. displayName)
end)

events.onUpdate(function(dt)
    local pad = world.findByName("SpawnPad")
    if pad ~= 0 then
        local x, y, z = world.getPosition(pad)
        world.setPosition(pad, x, MathUtil.clamp(y, 0, 10), z)
    end
end)

network.onServerEvent("requestSpawn", function(playerId)
    local entity = world.createEntity()
    world.setPosition(entity, 0, 5, 0)
end)
