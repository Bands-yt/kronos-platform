-- Kronos Lua Example: Moving Platform
--
-- Attach this to a real core::Script component on any entity that also
-- has a Transform (and a Renderable if you want it visible). Oscillates
-- the entity up and down around wherever it started -- a real,
-- self-contained example of per-tick world.getPosition/setPosition, not
-- a stub. See ../../docs/LUA_API.md's `world` section for the full
-- signature list this pulls from.

local selfId = world.findByName("MovingPlatform")

if selfId then
    local baseX, baseY, baseZ = world.getPosition(selfId)
    local elapsedSeconds = 0
    local amplitude = 2.0
    local speed = 1.5

    events.onUpdate(function(dt)
        elapsedSeconds = elapsedSeconds + dt
        local offset = math.sin(elapsedSeconds * speed) * amplitude
        world.setPosition(selfId, baseX, baseY + offset, baseZ)
    end)
else
    print("Moving Platform example: no entity named \"MovingPlatform\" in this scene -- rename an entity to try it")
end
