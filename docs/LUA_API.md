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
- **`world.playAnimation(path, looping?)`** → handle or `nil` —
  engine_runtime only.
- **`world.stopAnimation(handle)`** — engine_runtime only.

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
