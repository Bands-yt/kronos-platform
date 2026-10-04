-- Kronos engine script API type definitions, for Luau.Analysis
-- (Studio's native Script Editor -- see
-- studio/panels/ColorTextEditBackend.cpp's own LuauLiveAnalyzer). Loaded
-- via Frontend::loadDefinitionFile(), the same real mechanism Luau's own
-- CLI tools use for a host's global API surface.
--
-- This is a real, honest transcription of what this engine's C++ side
-- actually registers into a gameplay script's lua_State -- every global
-- table/function name and parameter here matches a real
-- lua_setglobal()/lua_setfield() call in one of:
--   core/Scripting.cpp        -- print, engine, task, events, require
--   core/ScriptWorldApi.cpp   -- world (entity/transform/physics/animation)
--   core/ScriptAvatarApi.cpp  -- world.spawnPlayer, avatar
--   core/ScriptNetworkApi.cpp -- network
--   core/ScriptUiApi.cpp      -- ui
--   core/ScriptChatApi.cpp    -- TextChatService
-- Not Roblox's `game`/`workspace`/`script` -- those don't exist in this
-- engine yet (see Scripting.cpp's own registerBindings() TODO on the
-- Instance/DataModel translation layer this would need). Declaring them
-- here anyway would make the type checker silently accept scripts that
-- crash at runtime with "attempt to index nil" -- worse than the honest
-- "unknown global" a real nonstrict-mode script currently gets away
-- with. Update this file in the same commit that adds or changes a
-- real global -- it drifts out of sync exactly like kKronosVersion does
-- if that rule isn't followed (see core/KronosVersion.hpp's own comment).

--- Writes its arguments to the Engine Console.
declare function print(...: any): ()
--- Loads another module script by path and returns its result.
declare function require(path: string): any

declare engine: {
    --- Logs a message at a level: "info", "warn" or "error".
    log: (level: string, message: string) -> (),
}

declare task: {
    --- Pauses this coroutine for the given seconds (one frame if omitted).
    wait: (seconds: number?) -> (),
    --- Runs a function immediately in a new coroutine.
    spawn: (fn: (...any) -> (...any), ...any) -> (),
    --- Runs a function in a new coroutine at the end of this frame.
    defer: (fn: (...any) -> (...any), ...any) -> (),
}

declare events: {
    --- Calls fn every simulation tick with the frame time in seconds.
    onUpdate: (fn: (dt: number) -> ()) -> (),
    --- Calls fn when two physics bodies start touching.
    onCollision: (fn: (entityA: number, entityB: number) -> ()) -> (),
    --- Calls fn when a player presses Interact on an entity.
    onInteract: (fn: (entity: number, interactor: number) -> ()) -> (),
    --- Calls fn just before this script is unloaded or hot-reloaded.
    onUnload: (fn: () -> ()) -> (),
    --- Calls fn when this client joins a multiplayer session.
    onSessionJoin: (fn: () -> ()) -> (),
    --- Calls fn when this client leaves a multiplayer session.
    onSessionLeave: (fn: () -> ()) -> (),
    --- Calls fn when any player joins the session.
    onPlayerJoin: (fn: (playerId: number, displayName: string) -> ()) -> (),
    --- Calls fn when any player leaves the session.
    onPlayerLeave: (fn: (playerId: number, displayName: string) -> ()) -> (),
}

type RaycastResult = {
    hit: boolean,
    entityId: number,
    x: number,
    y: number,
    z: number,
    nx: number,
    ny: number,
    nz: number,
    distance: number,
}

-- Kronos entity ids are plain numbers (a Luau double, matching every
-- numeric id this engine's whole Lua surface hands scripts -- see
-- ScriptUiApi.cpp's own luaJoinSession() comment on this same
-- convention), not a dedicated userdata/class type -- there is no
-- Instance hierarchy yet for one to belong to.
declare world: {
    --- Creates an empty entity and returns its id.
    createEntity: (name: string?) -> number,
    --- Parents child under parent; false if that would create a cycle.
    setParent: (child: number, parent: number) -> boolean,
    --- Detaches an entity from its parent.
    unparent: (entity: number) -> (),
    --- Returns the first entity with this name, or nil.
    findByName: (name: string) -> number?,
    --- Destroys an entity and its children.
    destroy: (entity: number) -> (),
    --- Returns an entity's world position as x, y, z.
    getPosition: (entity: number) -> (number, number, number),
    --- Moves an entity to a world position.
    setPosition: (entity: number, x: number, y: number, z: number) -> (),
    --- Returns an entity's rotation as Euler degrees x, y, z.
    getRotation: (entity: number) -> (number, number, number),
    --- Sets an entity's rotation from Euler degrees.
    setRotation: (entity: number, x: number, y: number, z: number) -> (),
    --- Sets an entity's scale on each axis.
    setScale: (entity: number, x: number, y: number, z: number) -> (),
    --- Tints an entity's material (0-1 channels).
    setColor: (entity: number, r: number, g: number, b: number, a: number?) -> (),
    --- Sets metallic and roughness (0-1).
    setMaterial: (entity: number, metallic: number, roughness: number) -> (),
    --- Makes an entity glow with a color and intensity.
    setEmissive: (entity: number, r: number, g: number, b: number, intensity: number) -> (),
    --- Pushes a physics body with an instant impulse.
    applyImpulse: (entity: number, x: number, y: number, z: number) -> (),
    --- Sets a physics body's linear velocity.
    setVelocity: (entity: number, x: number, y: number, z: number) -> (),
    --- Plays an animation clip; returns a handle for stopAnimation.
    playAnimation: (path: string, looping: boolean?) -> number?,
    --- Stops an animation started by playAnimation.
    stopAnimation: (handle: number) -> (),
    --- Casts a ray and returns the first hit, or nil.
    raycast: (originX: number, originY: number, originZ: number, dirX: number, dirY: number, dirZ: number,
        maxDistance: number) -> RaycastResult?,
    --- Spawns a physics box; returns its entity id.
    spawnDynamicBox: (x: number, y: number, z: number, halfExtentX: number, halfExtentY: number, halfExtentZ: number,
        mass: number, r: number?, g: number?, b: number?) -> number?,
    -- Added by ScriptAvatarApi::registerInto() onto this same table, not
    -- a second `world` -- see that function's own comment on why it
    -- requires ScriptWorldApi to have registered first.
    --- Spawns a player character at a position.
    spawnPlayer: (x: number, y: number, z: number) -> number?,
}

declare avatar: {
    -- Returns (played: boolean) on success, or (false, error: string) --
    -- Luau can't express "second return only present when the first is
    -- false" any more precisely than an optional second value.
    --- Plays an emote on an avatar entity.
    playEmote: (entity: number, emoteId: string, looping: boolean?) -> (boolean, string?),
}

declare network: {
    --- Sends a named event with an optional payload to the server.
    fireServer: (name: string, payload: {[string]: any}?) -> (),
    --- Server only: sends a named event to every client.
    fireAllClients: (name: string, payload: {[string]: any}?) -> (),
    --- Server only: handles a named event sent by fireServer.
    onServerEvent: (name: string, fn: (sender: number, payload: {[string]: any}) -> ()) -> (),
    --- Client only: handles a named event sent by fireAllClients.
    onClientEvent: (name: string, fn: (payload: {[string]: any}) -> ()) -> (),
}

declare ui: {
    --- Draws text on screen this frame (pixels from top-left).
    drawText: (text: string, x: number, y: number, scale: number?, r: number?, g: number?, b: number?,
        a: number?) -> (),
    --- Draws a filled rectangle on screen this frame.
    drawRect: (x: number, y: number, w: number, h: number, r: number?, g: number?, b: number?, a: number?) -> (),
    --- Opens the built-in session browser.
    sessionBrowser: () -> (),
    --- Toggles the built-in player list.
    playerList: () -> (),
    --- Joins a session by id.
    joinSession: (sessionId: number) -> (),
    --- Leaves the current session.
    leaveSession: () -> (),
}

type ChatMessage = {
    senderId: number,
    channel: string,
    body: string,
    timestamp: number,
}

declare TextChatService: {
    -- channel: "General" | "Team" | "Whisper" -- ScriptChatApi.cpp's own
    -- channelFromName() rejects anything else (including "System",
    -- deliberately -- see that function's own comment), but Luau has no
    -- string-literal-union-from-a-C++-list mechanism here, so this stays
    -- `string` rather than a fabricated-looking union that could go
    -- stale against the real list.
    --- Sends a chat message on a channel ("General" by default).
    SendAsync: (message: string, channel: string?) -> (),
    --- Calls fn for every chat message received.
    OnIncomingMessage: (fn: (message: ChatMessage) -> ()) -> (),
}
