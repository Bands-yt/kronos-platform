-- Hello World -- the smallest real starter plugin.
--
-- Proves the Luau VM, task/events tables, and the load/reload cycle all
-- work end to end, with nothing else to distract from that. If this
-- loads cleanly and you see "Hello World: loaded" in the Debug Console,
-- your plugin pipeline is working -- copy this folder as your own
-- starting point, or use templates/plugin/ for a slightly more
-- annotated version of the same thing.

print("Hello World: loaded")

local tickCount = 0
events.onUpdate(function(dt)
    tickCount = tickCount + 1
end)

events.onUnload(function()
    print("Hello World: unloaded after " .. tostring(tickCount) .. " ticks")
end)
