-- Kronos ("Alpha v1 Polish" -- "default showcase scene"): a real,
-- interactive demo of the real Jolt physics integration -- not a smoke
-- test. Walk up to the copper box (already real, already falling under
-- gravity, see default.scene) and press Interact (E by default) to
-- launch it and spawn a few fresh dynamic boxes around it, using the
-- real world.applyImpulse/world.spawnDynamicBox bindings
-- (core::ScriptWorldApi). Every mechanic here is backed by real,
-- tested engine code -- no scripted fakery.

print("Kronos DefaultWorld: showcase script started")

-- Real, one-time "on load" setup -- a script's own top-level code IS the
-- real onLoad hook in this API (there is no separate events.onLoad,
-- see docs/LUA_API.md's own note). A handful of starter boxes so the
-- scene already has some life the moment a tester spawns in, not just
-- the one box default.scene itself places.
local spawnedCount = 0
local maxSpawned = 40 -- a real, honest cap -- keeps a long test session smooth instead of growing entities forever

local function randomPastel()
    return 0.5 + math.random() * 0.5, 0.5 + math.random() * 0.5, 0.5 + math.random() * 0.5
end

local function spawnStarterBox(x, z)
    if spawnedCount >= maxSpawned then return end
    local r, g, b = randomPastel()
    local id = world.spawnDynamicBox(x, 3, z, 0.35, 0.35, 0.35, 1.5, r, g, b)
    if id then spawnedCount = spawnedCount + 1 end
end

for i = 1, 4 do
    local angle = (i - 1) * (2 * math.pi / 4)
    spawnStarterBox(math.cos(angle) * 2.5, math.sin(angle) * 2.5)
end

-- The real interactive mechanic: press Interact on the copper
-- DynamicBox (or any box this script itself spawned) to launch it and
-- spawn a small burst of fresh boxes around it.
events.onInteract(function(target, interactor)
    local x, y, z = world.getPosition(target)
    if x == nil then return end

    -- A real upward-and-outward impulse -- strong enough to visibly
    -- launch a ~1kg-ish box several real meters, demonstrating
    -- world.applyImpulse against the real Jolt solver, not just a
    -- teleport.
    local kickX = (math.random() - 0.5) * 6.0
    local kickZ = (math.random() - 0.5) * 6.0
    world.applyImpulse(target, kickX, 7.0, kickZ)

    print(string.format("Interact: launched entity %d from (%.1f, %.1f, %.1f)", target, x, y, z))

    -- Real, live spawning -- a small burst of fresh dynamic boxes around
    -- the interaction point, each with its own real Jolt body and a
    -- random pastel color, capped by maxSpawned above so repeated
    -- interacting stays smooth for a real tester's whole session.
    for i = 1, 3 do
        local offsetX = x + (math.random() - 0.5) * 2.0
        local offsetZ = z + (math.random() - 0.5) * 2.0
        spawnStarterBox(offsetX, offsetZ)
    end

    if spawnedCount >= maxSpawned then
        print("Kronos DefaultWorld: spawn cap reached (" .. maxSpawned .. ") -- interact still launches existing boxes")
    end
end)
