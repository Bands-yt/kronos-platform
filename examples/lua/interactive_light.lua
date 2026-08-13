-- Kronos Lua Example: Interactive Light
--
-- Attach to an entity with a Transform + Renderable named "InteractiveLight".
-- Toggles between a dim and a bright emissive color each time a real
-- proximity/interact trigger fires against this entity (see
-- ../../docs/LUA_API.md's `events.onInteract` entry). A real,
-- self-contained example of world.setEmissive plus events.onInteract,
-- not a stub.
--
-- engine_runtime only: world.setEmissive() isn't part of Studio's own,
-- smaller `world` table (see ../../docs/LUA_API.md's availability
-- table) -- this example is written for a gameplay Script component,
-- not a Studio plugin.

local selfId = world.findByName("InteractiveLight")

if selfId then
    local isLit = false
    world.setEmissive(selfId, 0.1, 0.1, 0.1, 0.2)

    events.onInteract(function(entity, interactor)
        if entity ~= selfId then
            return
        end
        isLit = not isLit
        if isLit then
            world.setEmissive(selfId, 1.0, 0.85, 0.4, 3.0)
        else
            world.setEmissive(selfId, 0.1, 0.1, 0.1, 0.2)
        end
    end)
else
    print("Interactive Light example: no entity named \"InteractiveLight\" in this scene -- rename an entity to try it")
end
