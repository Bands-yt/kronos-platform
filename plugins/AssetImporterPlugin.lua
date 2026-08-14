-- Asset Importer -- an honest placeholder, not a real importer.
--
-- What this ISN'T: a way to bring external assets (textures, models)
-- into a scene from Luau. There is no asset-registry binding in either
-- `world` table anywhere in this engine yet, and Studio's own `world`
-- table doesn't even have createEntity -- a plugin script can't spawn
-- new content at all today, only act on entities that already exist.
-- Real asset import (Model Importer, Texture Preview) is native C++
-- Studio plugin functionality, not something exposed to Luau.
--
-- What this IS: a demonstration of the one adjacent real capability a
-- script *can* use -- restyling an existing entity via findByName +
-- setColor/setPosition, clearly labeled as a stand-in below, not a real
-- import. If you're looking to actually import a model, use Studio's
-- native "Model Importer" plugin (Plugins menu -> World).

print("Asset Importer: no asset-registry Lua binding exists yet -- this plugin can't import real content.")
print("Asset Importer: demonstrating the closest real thing instead -- restyling an existing named entity.")

local targetName = "StarterBox"
local target = world.findByName(targetName)
if target == nil then
    print("Asset Importer: \"" .. targetName .. "\" not found in the current scene -- nothing to demonstrate on.")
else
    world.setColor(target, 0.2, 0.8, 0.4, 1.0)
    print("Asset Importer: recolored \"" .. targetName .. "\" as a stand-in for a real import step.")
end

events.onUnload(function()
    print("Asset Importer: unloaded")
end)
