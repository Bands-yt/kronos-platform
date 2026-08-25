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

declare function print(...: any): ()
declare function require(path: string): any

declare engine: {
    log: (level: string, message: string) -> (),
}

declare task: {
    wait: (seconds: number?) -> (),
    spawn: (fn: (...any) -> (...any), ...any) -> (),
    defer: (fn: (...any) -> (...any), ...any) -> (),
}

declare events: {
    onUpdate: (fn: (dt: number) -> ()) -> (),
    onCollision: (fn: (entityA: number, entityB: number) -> ()) -> (),
    onInteract: (fn: (entity: number, interactor: number) -> ()) -> (),
    onUnload: (fn: () -> ()) -> (),
    onSessionJoin: (fn: () -> ()) -> (),
    onSessionLeave: (fn: () -> ()) -> (),
    onPlayerJoin: (fn: (playerId: number, displayName: string) -> ()) -> (),
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
    createEntity: (name: string?) -> number,
    setParent: (child: number, parent: number) -> boolean,
    unparent: (entity: number) -> (),
    findByName: (name: string) -> number?,
    destroy: (entity: number) -> (),
    getPosition: (entity: number) -> (number, number, number),
    setPosition: (entity: number, x: number, y: number, z: number) -> (),
    getRotation: (entity: number) -> (number, number, number),
    setRotation: (entity: number, x: number, y: number, z: number) -> (),
    setScale: (entity: number, x: number, y: number, z: number) -> (),
    setColor: (entity: number, r: number, g: number, b: number, a: number?) -> (),
    setMaterial: (entity: number, metallic: number, roughness: number) -> (),
    setEmissive: (entity: number, r: number, g: number, b: number, intensity: number) -> (),
    applyImpulse: (entity: number, x: number, y: number, z: number) -> (),
    setVelocity: (entity: number, x: number, y: number, z: number) -> (),
    playAnimation: (path: string, looping: boolean?) -> number?,
    stopAnimation: (handle: number) -> (),
    raycast: (originX: number, originY: number, originZ: number, dirX: number, dirY: number, dirZ: number,
        maxDistance: number) -> RaycastResult?,
    spawnDynamicBox: (x: number, y: number, z: number, halfExtentX: number, halfExtentY: number, halfExtentZ: number,
        mass: number, r: number?, g: number?, b: number?) -> number?,
    -- Added by ScriptAvatarApi::registerInto() onto this same table, not
    -- a second `world` -- see that function's own comment on why it
    -- requires ScriptWorldApi to have registered first.
    spawnPlayer: (x: number, y: number, z: number) -> number?,
}

declare avatar: {
    -- Returns (played: boolean) on success, or (false, error: string) --
    -- Luau can't express "second return only present when the first is
    -- false" any more precisely than an optional second value.
    playEmote: (entity: number, emoteId: string, looping: boolean?) -> (boolean, string?),
}

declare network: {
    fireServer: (name: string, payload: {[string]: any}?) -> (),
    fireAllClients: (name: string, payload: {[string]: any}?) -> (),
    onServerEvent: (name: string, fn: (sender: number, payload: {[string]: any}) -> ()) -> (),
    onClientEvent: (name: string, fn: (payload: {[string]: any}) -> ()) -> (),
}

declare ui: {
    drawText: (text: string, x: number, y: number, scale: number?, r: number?, g: number?, b: number?,
        a: number?) -> (),
    drawRect: (x: number, y: number, w: number, h: number, r: number?, g: number?, b: number?, a: number?) -> (),
    sessionBrowser: () -> (),
    playerList: () -> (),
    joinSession: (sessionId: number) -> (),
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
    SendAsync: (message: string, channel: string?) -> (),
    OnIncomingMessage: (fn: (message: ChatMessage) -> ()) -> (),
}
