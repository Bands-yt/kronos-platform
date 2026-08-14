-- UI Inspector -- a real, honestly-scoped entity watcher.
--
-- What this ISN'T: an Explorer-selection inspector. Studio's own
-- `world` table (studio::registerStudioEcsBindings) has exactly six
-- functions -- findByName, getPosition, setPosition, setColor,
-- setParent, unparent -- and none of them expose "what's currently
-- selected in the Explorer panel." That's a real gap, not something
-- this script can work around from Luau.
--
-- What this IS: a real, working watcher for one NAMED entity, polling
-- its real position every couple of seconds via world.findByName() +
-- world.getPosition(). Set watchedEntityName below to something that
-- exists in your current scene (e.g. "StarterBox" in the default
-- project template) and reload.

local watchedEntityName = "StarterBox"
local pollIntervalSeconds = 2.0
local elapsedSinceLastPoll = 0.0

events.onUpdate(function(dt)
    elapsedSinceLastPoll = elapsedSinceLastPoll + dt
    if elapsedSinceLastPoll < pollIntervalSeconds then return end
    elapsedSinceLastPoll = 0.0

    local entity = world.findByName(watchedEntityName)
    if entity == nil then
        print("UI Inspector: \"" .. watchedEntityName .. "\" not found in the current scene")
        return
    end
    local x, y, z = world.getPosition(entity)
    print(string.format("UI Inspector: \"%s\" (id %d) at (%.2f, %.2f, %.2f)", watchedEntityName, entity, x, y, z))
end)

events.onUnload(function()
    print("UI Inspector: unloaded")
end)
