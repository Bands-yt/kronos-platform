# Kronos Platform — Lua API Reference

The real, complete Luau surface exposed to a gameplay script (attached via
a real `core::Script` component, see [COMPONENT_SYSTEM.md](COMPONENT_SYSTEM.md))
or a Studio scripted plugin (see [PLUGIN_SYSTEM.md](PLUGIN_SYSTEM.md)).
Every function below is real and tested — this is not aspirational API
surface.

Deliberately **not** shaped like Roblox's own Instance/DataModel API (no
`game`/`workspace` globals, no `Instance.new()`, no property-indexing like
`part.Position = Vector3.new(x, y, z)`). Building a partial look-alike of
that API would be misleading — there is no Instance/DataModel translation
layer in this engine. Entities are plain numbers (the underlying
`core::EntityId`, opaque to script) and every function is a flat call on
a global table: `world.setPosition(id, x, y, z)`.

## Availability by context

| Global | engine_runtime (gameplay `Script`) | Studio `ScriptedPlugin` | Studio Debug Console |
|---|---|---|---|
| `print`, `engine.log`, `task.*`, `events.*` | ✅ | ✅ | ✅ |
| `world.*` (full) | ✅ (`core::ScriptWorldApi`) | ✅ (smaller — no Physics/Animation, see below) | ✅ (same as plugins) |
| `world.spawnPlayer`, `avatar.*` | ✅ (`core::ScriptAvatarApi`) | ❌ | ❌ |
| `network.*` | ✅ | ✅ | ❌ |
| `ui.*` | ✅ | ❌ | ❌ |

Studio's plugin/console `world` table (`studio::registerStudioEcsBindings`)
is intentionally smaller than engine_runtime's (`core::ScriptWorldApi`) —
Studio owns no live `Physics`/`RuntimeAnimationPlayer`, so
`applyImpulse`/`setVelocity`/`playAnimation`/`stopAnimation` don't exist
there. It's also missing `createEntity`, `getRotation`/`setRotation`,
`setScale`, and `setMaterial`/`setEmissive` — a Studio plugin/console
script can only act on entities that already exist (via `findByName`),
not spawn new ones. The two tables share exactly six real functions:
`findByName`/`getPosition`/`setPosition`/`setColor`/`setParent`/
`unparent`.

## Core globals

- **`print(...)`** — same as stock Luau `print`, also forwarded to
  `core::Logger` (category `"Script"`, level `Info`) and to any live
  output-capturing UI (Studio's Debug Console / a `ScriptedPlugin`'s log).
- **`engine.log(level, message)`** — `level` is a string (`"Info"`,
  `"Warn"`, etc., freeform), forwarded the same way as `print`.
- **`require(modulePath)`** — loads a module through Kronos's own virtual
  file system. **There is no filesystem access**: every lookup goes to a
  host-installed resolver, and paths containing `..`, absolute paths, and
  embedded NULs are rejected before the resolver is even consulted. The
  result is cached per script, so requiring the same module twice returns
  the identical value and never re-runs its side effects. A module is
  *not* a privilege boundary — it runs at the requiring script's own
  security identity and shares its execution and memory budgets. Resolvers
  are given the caller's identity and may refuse to serve internal
  modules to user scripts.

## Sandbox and security identities

Every script runs in its own Luau VM with its own global table, its own
memory ceiling, and a fixed **security identity**:

| Identity | Level | Used for |
|---|---|---|
| `UserScript` | 0 | User-generated content. Assume the author is hostile. |
| `CoreScript` | 4 | Engine-shipped gameplay scripts. |
| `StudioPlugin` | 6 | Editor plugins needing Studio-only APIs. |

The identity is fixed when the VM is created and can never be raised.
There is deliberately **no Lua-visible way to read, set, or escalate it**,
and coroutines inherit their creator's identity — so doing work inside
`coroutine.create()` grants nothing extra. Omitting an identity when
loading a script yields level 0, because a forgotten argument must fail
closed.

Privilege is enforced primarily by *capability*, not by runtime checks:
an elevated API is simply never registered into a lower-privileged VM's
global table, so an under-privileged script has no reference to reach.

**Not available to any script, at any identity:** `io`, `package`,
`loadstring`, `load`, `dofile`, `loadfile`, `collectgarbage`, `getfenv`,
`setfenv`, `newproxy`, `os.execute`/`getenv`/`remove`/`rename`/`exit`,
and every `debug` entry except `debug.traceback`. `_G`, the builtin
libraries, and the string metatable are all frozen.

Runaway scripts are bounded rather than trusted: exceeding the per-tick
execution budget (8 ms by default) or the per-script memory ceiling
(256 MB by default) raises an ordinary catchable Luau error and kills
only that script — never the host process.

## `task` — scheduling

Roblox-style task scheduling, backed by real Luau coroutines.

- **`task.wait(seconds)`** — yields the current coroutine; resumes once
  at least `seconds` of scheduler time have elapsed (real, coroutine-based,
  not a busy-loop).
- **`task.spawn(fn, ...)`** — runs `fn` on a new coroutine immediately.
- **`task.defer(fn, ...)`** — runs `fn` on a new coroutine at the start of
  the *next* scheduler tick.

## `events` — lifecycle and world callbacks

A real, broadcast event bus: every registered handler across every script
loaded into a given `core::Scripting` instance fires on each real event —
not a per-entity `:Connect()`-style signal system.

- **`events.onUpdate(fn)`** — `fn(dt)`, fires once per real scheduler
  tick.
- **`events.onCollision(fn)`** — `fn(entityA, entityB)`, fires on a real
  Physics contact event.
- **`events.onInteract(fn)`** — `fn(entity, interactor)`, fires on a real
  proximity/interact trigger.
- **`events.onUnload(fn)`** — `fn()`, fires when the owning `Scripting`
  instance's `shutdown()` is called *explicitly* (e.g. a Studio scripted
  plugin's real `reload()`) — **not** on final process/object teardown,
  see [PLUGIN_SYSTEM.md](PLUGIN_SYSTEM.md) §3 for the real bug this
  asymmetry was built to fix. The one lifecycle hook worth registering
  explicitly; `onLoad` is just your script's own top-level code running,
  and `onUpdate` above already covers per-tick logic.
- **`events.onSessionJoin(fn)`** / **`events.onSessionLeave(fn)`** —
  `fn()`, fire when this process's own real multiplayer session actually
  starts/ends (server: real `NetworkSession::initialize()` success and
  graceful `shutdown()`; client: a real, accepted join handshake and a
  real disconnect/leave, graceful or not). See
  [MULTIPLAYER_SESSION_UX.md](MULTIPLAYER_SESSION_UX.md) for the real
  join/leave flow these are wired to.
- **`events.onPlayerJoin(fn)`** / **`events.onPlayerLeave(fn)`** —
  `fn(playerId, displayName)`, mirroring Roblox's own real
  `Players.PlayerAdded`/`PlayerRemoving` shape. Fire on **either** role:
  server-side, from the real join handshake/disconnect cleanup;
  client-side, from the real `PlayerRosterJoined`/`PlayerRosterLeft`
  broadcasts (a joining client also gets a real, retroactive
  `onPlayerJoin` for every player already in the session, not just future
  joins — matching Roblox's own documented `PlayerAdded` behavior).

## `world` — entities

Every function takes/returns a plain entity id (a number), or `nil` where
noted.

- **`world.createEntity(name?)`** → id — creates a real, bare entity
  (a `Transform`, plus a `Name` if given). No default mesh — an entity
  needs a Renderable attached some other way to be visible; this is a
  deliberate, honest minimum (see `ScriptWorldApi.hpp`'s own comment).
- **`world.destroy(id)`** — destroys the entity.
- **`world.findByName(name)`** → id or `nil`.
- **`world.getPosition(id)`** → x, y, z (or nothing if the entity has no
  `Transform`).
- **`world.setPosition(id, x, y, z)`**
- **`world.getRotation(id)`** → x, y, z (Euler degrees).
- **`world.setRotation(id, x, y, z)`** (Euler degrees).
- **`world.setScale(id, x, y, z)`**
- **`world.setColor(id, r, g, b, a?)`** — sets `Renderable::baseColor`;
  no-op if the entity has no `Renderable`.
- **`world.setMaterial(id, metallic, roughness)`**
- **`world.setEmissive(id, r, g, b, intensity)`**
- **`world.setParent(child, parent)`** → `true`/`false` — real,
  validated reparent (`core::hierarchy::setParent()`); rejects self-
  parenting and cycles, see [SCENE_SYSTEM.md](SCENE_SYSTEM.md).
- **`world.unparent(id)`** — detaches from its parent, preserving real
  world position.
- **`world.applyImpulse(id, x, y, z)`** — engine_runtime only (needs
  `Physics`).
- **`world.setVelocity(id, x, y, z)`** — engine_runtime only.
- **`world.spawnDynamicBox(x, y, z, halfExtentX, halfExtentY, halfExtentZ, mass, r?, g?, b?)`**
  → id, or `nil` — engine_runtime only. Real, wraps
  `core::Physics::createDynamicBox()`: a genuine dynamic Jolt rigid body
  plus a visible mesh (a shared, pre-registered box mesh, scaled to the
  requested half-extent — no live GPU work happens on this call).
  Returns `nil` if no spawn-box mesh handle has been registered yet
  (only possible if you're embedding `ScriptWorldApi` somewhere that
  never calls `Application::setScriptSpawnBoxMeshHandle()` — real
  `engine_runtime` launches always have one). `halfExtent` components
  are clamped to `[0.05, 5.0]` and `mass` to `[0.01, 500]` before
  reaching Jolt — a script-supplied shape is never handed to the physics
  engine unclamped. `r`/`g`/`b` default to a neutral gray (`0.8`) if
  omitted. Like every other real Physics position write in this API,
  the spawned box's position is correct via `world.getPosition()` only
  after the *next* real physics step.
  ```lua
  local id = world.spawnDynamicBox(0, 5, 0, 0.5, 0.5, 0.5, 1.5, 0.9, 0.3, 0.3)
  if id then world.applyImpulse(id, 0, 3, 0) end
  ```
- **`world.playAnimation(path, looping?)`** → handle or `nil` —
  engine_runtime only.
- **`world.stopAnimation(handle)`** — engine_runtime only.
- **`world.raycast(originX, originY, originZ, dirX, dirY, dirZ, maxDistance)`**
  → a result table, or `nil` on a miss — engine_runtime only (needs
  `Physics`). `direction` need not be normalized; `maxDistance` is the
  ray's real length regardless of `direction`'s own magnitude. On a hit,
  returns `{hit = true, entityId = <number>, x = , y = , z = , nx = ,
  ny = , nz = , distance = }` — flat `x/y/z`/`nx/ny/nz` fields (the hit
  point and surface normal), not a nested "Vector3" sub-table, matching
  every other position-shaped value in this API (`getPosition` etc.).
  ```lua
  local result = world.raycast(0, 1, 0, 0, 0, 1, 20)
  if result then
      print("hit entity " .. result.entityId .. " at distance " .. result.distance)
  end
  ```
- **`world.spawnPlayer(x, y, z)`** → id or `nil` — engine_runtime only.
  Real player spawn/respawn control, scoped to the local player (no
  per-player targeting concept exists in this engine — see
  `ScriptAvatarApi.hpp`'s own header comment). If no avatar is currently
  spawned, does a real, fresh spawn at `(x, y, z)` with default cosmetics;
  if one already exists, real-teleports it there instead (a "respawn").
  Like every other Physics position write in this API
  (`applyImpulse`/`setVelocity`), the move takes effect on the real Jolt
  body immediately but only becomes visible via `world.getPosition()`
  after the *next* real physics step — reading it back in the same tick
  as the `spawnPlayer()` call sees the pre-move position.
  ```lua
  local playerId = world.spawnPlayer(0, 2, 0)
  ```

## `avatar` — local player avatar control

engine_runtime only. Real, but currently scoped to the local player only
— there is no per-entity `AvatarController` registry yet (only ever "the"
local player has a live one), so `entity` below is checked, not just
accepted — any id other than the real local player entity gets an
honest `false`, not a silent no-op pretending to succeed.

- **`avatar.playEmote(entity, emoteName, looping?)`** → `true`/`false`,
  `errorMessage?` — resolves `emoteName` against the real catalogue/
  animation-database link (`core::resolveEmoteClip`, same convention a
  purchasable Emote item's id already follows) and plays it full-body on
  the local player. Returns `false` (with no error message) if no avatar
  is currently spawned, the avatar catalogue hasn't loaded yet, or
  `entity` isn't the local player; returns `false` with a real error
  message only when `emoteName` itself is a genuine data problem (listed
  in the catalogue but its clip fails to load).
  ```lua
  local playerId = world.spawnPlayer(0, 2, 0)
  local played, err = avatar.playEmote(playerId, "wave")
  if not played and err then
      print("emote failed: " .. err)
  end
  ```

## `network` — real client/server RPC

See [NETWORKING_UPGRADE.md](NETWORKING_UPGRADE.md) for the full real wire
protocol underneath this. A payload is a plain Lua table with string keys
mapping to number/string/boolean values.

- **`network.fireServer(name, payload?)`** — client-only; real, honest
  no-op outside Client mode.
- **`network.onServerEvent(name, fn)`** — `fn(player, payload)`,
  server-side handler registration.
- **`network.fireAllClients(name, payload?)`** — server-only broadcast to
  every connected client; real, honest no-op outside Server mode.
- **`network.onClientEvent(name, fn)`** — `fn(payload)`, client-side
  handler registration.

## `ui` — screen-space HUD drawing and shell control

engine_runtime only. See `ScriptUiApi.hpp`'s own header comment for the
real, honest scope boundary: this only actually renders anything during
the two real contexts `core::Application` already drives a per-frame
`UIRenderer` pass from today (camera-showcase mode, an active TNT Wars
match) — not a general always-on HUD for every scene.

- **`ui.drawText(text, x, y, scale?, r?, g?, b?, a?)`** — real, monospace
  bitmap text, screen-space pixel coordinates (origin top-left).
- **`ui.drawRect(x, y, w, h, r?, g?, b?, a?)`** — real, flat-color
  screen-space rectangle.

Both queue a draw command; nothing appears until the next real
`UIRenderer` flush that frame. A script that wants something to stay on
screen calls these every tick, same as any other per-frame draw call in
this codebase.

Kronos ("Active Joining UI"): four more bindings drive the same real
pre-game shell (`runtime::RuntimeShell`, see that class's own comment)
that the Home Screen/Session Browser panels' native buttons already
drive — a script doesn't get a second, independently-drifting join/leave
path, it calls into the exact same `joinSession()`/`leaveSession()`
methods.

- **`ui.sessionBrowser()`** — switches the shell to the Session Browser
  panel (same as clicking "Sessions" on the Home Screen).
- **`ui.playerList()`** — toggles the Player List overlay.
- **`ui.joinSession(id)`** — looks `id` up among the sessions the real
  LAN browser has currently discovered and, if found, joins it — the
  same real join path (`Loading` → `InGame`/`Error`, driven by
  `net::NetworkSession`'s real join-accepted/rejected result) the native
  "Join" button uses.
- **`ui.leaveSession()`** — leaves the current session, same as the
  native "Leave" button.

All four are real, honest no-ops (never an error) when called from a
context with no `core::ScriptShellController` wired up — i.e. anywhere
other than `engine_runtime`'s plain, no-flag Home Screen launch path
(see `main.cpp`'s `homeScreenMode` gating); Studio's `ScriptedPlugin`
and Debug Console never set one, so these are `ui.*`-table members that
exist everywhere `ui` does but only ever act in `engine_runtime`.
