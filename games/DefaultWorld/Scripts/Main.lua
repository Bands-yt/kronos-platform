print("engine_runtime: bring-up script started")
task.spawn(function()
    for i = 1, 3 do
        task.wait(1)
        print("tick from spawned coroutine: " .. i)
    end
end)
task.defer(function()
    print("deferred: runs after this resumption cycle")
end)

-- world/events smoke test: the real entity/material/physics API
-- surface (core/ScriptWorldApi.hpp) and the real event bus
-- (Scripting.hpp's events table), both exercised against the
-- actual DefaultWorld scene, not a synthetic example.
local box = world.findByName("DynamicBox")
if box then
    local x, y, z = world.getPosition(box)
    print(string.format("world.findByName('DynamicBox') -> id=%d at (%.1f, %.1f, %.1f)", box, x, y, z))
    world.setColor(box, 0.2, 0.9, 1.0, 1.0)
    world.setMaterial(box, 0.1, 0.3)
end

local updateCount = 0
events.onUpdate(function(dt)
    updateCount = updateCount + 1
    if updateCount == 1 then
        print("events.onUpdate: first tick, dt=" .. dt)
    end
end)

events.onCollision(function(a, b)
    print(string.format("events.onCollision: entity %d touched entity %d", a, b))
end)

events.onInteract(function(target, interactor)
    print(string.format("events.onInteract: entity %d interacted with by entity %d", target, interactor))
end)
