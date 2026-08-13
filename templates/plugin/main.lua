-- Kronos Studio Plugin Template
--
-- A real, minimal, working plugin -- copy this whole folder, rename
-- example.manifest and this file's ENTRY reference if you like, and edit
-- freely. Demonstrates the real `world`, `network`, `events`, and `task`
-- tables a scripted plugin gets (see ../../docs/PLUGIN_SYSTEM.md and
-- ../../docs/LUA_API.md for the full reference).
--
-- Studio's own `world` table is intentionally SMALLER than a gameplay
-- script's `world` table -- no createEntity/destroy/applyImpulse/
-- playAnimation (Studio has no live Physics/Animation system, and no
-- Vulkan mesh-building handles this binding could use to give a new
-- entity a real mesh either). What IS real here: findByName,
-- getPosition, setPosition, setColor, setParent, unparent -- see
-- ../../docs/LUA_API.md's own availability table for the exact list.

print("Example Plugin loaded")

-- Real per-frame logic -- fires once per real Studio tick while this
-- plugin is loaded (not gated on its own window being open -- see
-- studio::plugins::ScriptedPlugin's own class comment).
local elapsedSeconds = 0
events.onUpdate(function(dt)
    elapsedSeconds = elapsedSeconds + dt
end)

-- Real entity interaction -- world.findByName() looks up an entity
-- that's already in the currently-open scene by its real Name component.
-- Try this in a scene that has an entity named "Target":
--
-- local target = world.findByName("Target")
-- if target then
--     world.setPosition(target, 0, 5, 0)
--     world.setColor(target, 0.3, 0.8, 1.0, 1.0)
-- end

-- Real networking -- a real, honest no-op unless Studio's own Network
-- Overlay plugin has actually started a session (see
-- ../../docs/NETWORKING_UPGRADE.md). Uncomment to try it once you've
-- hosted or joined a session from the Network Overlay panel:
--
-- network.onServerEvent("ExamplePing", function(player, payload)
--     print("received ExamplePing from player " .. tostring(player))
-- end)
-- network.fireServer("ExamplePing", {message = "hello from the plugin template"})

-- Real cleanup -- fires when this plugin is reloaded (Studio's own
-- "Reload" button in its window) or when Studio itself shuts down,
-- giving you a real chance to undo anything you set up above before the
-- next version's own code runs. See docs/PLUGIN_SYSTEM.md's own section
-- 2 for exactly when this does (and doesn't) fire.
events.onUnload(function()
    print("Example Plugin unloaded after " .. tostring(elapsedSeconds) .. "s")
end)
