# Kronos Alpha — Section 5: Lua Creator Experience

Status of the [Alpha Completion Checklist](ALPHA_COMPLETION_CHECKLIST.md)'s
Section 5. Verified with a full rebuild (`engine_runtime`, `studio`,
`engine_tests`) and the full test suite green (9562/9562 checks passing,
up from 9530 — 32 new tests, one of them a real regression test for a
real bug this audit found and fixed).

## Lua API reference

Already real and complete — [LUA_API.md](LUA_API.md), built earlier this
session, with a real availability table distinguishing engine_runtime's
full `world` binding from Studio's smaller one. No changes needed here;
this section's real gap turned out to be examples, not reference material.

## Lua examples

**Built — `examples/lua/`** (new, three real, standalone gameplay-`Script`
examples, distinct from Section 4's plugin template):

- `moving_platform.lua` — `world.getPosition`/`setPosition` driven from
  `events.onUpdate`, oscillating an entity around wherever it started.
- `interactive_light.lua` — `world.setEmissive` toggled from a real
  `events.onInteract` handler; documented inline as engine_runtime-only
  (Studio's smaller `world` table has no `setEmissive`, caught while
  writing this example's own test — see below).
- `networked_ping.lua` — one real, shared file loaded on both a server
  and client `Scripting` instance, demonstrating `network.fireServer`/
  `onServerEvent`/`fireAllClients`/`onClientEvent` as a real ping/pong
  round trip.

Each is validated by a real test that loads the *actual shipped file*
(not a re-typed copy) end-to-end: `testLuaExampleMovingPlatformOscillatesRealPosition`
confirms a real tick genuinely moves the Y position while X/Z stay fixed;
`testLuaExampleInteractiveLightTogglesRealEmissive` confirms two real
`fireInteract()` calls really toggle `Renderable::emissiveIntensity` on
and back off; `testLuaExampleNetworkedPingRealRoundTrip` runs a real
two-socket client/server pair (mirroring
`testScriptNetworkApiRealFireServerReachesLuauHandler`'s own pattern) and
confirms the client's own Ping genuinely reaches the server's handler,
which genuinely broadcasts a Pong the client's own handler genuinely
receives.

## Lua error formatting

Audited, already real and creator-legible: `Scripting::loadAndRun()`
compiles with `Luau::CompileOptions::debugLevel = 1`, so every runtime
error Luau itself throws already carries a real `chunkName:line:` prefix
(e.g. `HotReloadEntity:3: attempt to index nil value`) before this
engine's own `"compile error in ..."`/`"runtime error in ..."` wrapper is
added — a creator debugging a script gets a real source line, not just
"something broke." No change needed.

## Lua hot-reload stability

**Real bug found and fixed.** `Scripting::unload()` carried a comment
claiming it was safe to leave stale `parked_`/`deferredQueue_`/
`onUpdateCallbacks_`/`onCollisionCallbacks_`/`onInteractCallbacks_`/
`onUnloadCallbacks_` entries behind after closing a script's `lua_State`,
reasoning "nothing yet calls unload() outside of shutdown." That
reasoning was already false: `core::Application`'s own real hot-reload
path (Alpha Roadmap Phase 7) calls `unload()` on exactly one script
inside a Scripting instance shared with every other live script, every
time a `Script` component's source changes — it cannot use a full
`shutdown()`, since that would kill every other script too.

**The real, reachable consequence**: any hot-reloaded script that had
registered `events.onUpdate` (or `onCollision`/`onInteract`/`onUnload`,
or had a live `task.wait()` coroutine parked) left a dangling
`lua_State*` behind. The very next `scripting_.tick()` call —
`Application.cpp` runs it in the same frame, right after the hot-reload
scan — would resume/invoke a freed VM. `examples/lua/moving_platform.lua`,
built for this same section, uses exactly this pattern
(`events.onUpdate`), so this was not a theoretical corner case.

**Why the existing test never caught it**: `testScriptComponentHotReloadsOnSourceChange`
(Phase 7) hot-reloads a script that only runs top-level code — it never
registers an event handler, so it never touched the dangling path.

**Fixed** — `Scripting::unload()` now filters all five lists by
`lua_mainthread(entry.thread) == script.owner` (parked/deferred queues)
or `callback.owner == script.owner` (event callback lists) before
`lua_close()`, dropping only the entries that belong to the script being
unloaded. Deliberately does *not* fire the unloaded script's own
`onUnload` handler — that stays scoped to a real `Scripting::shutdown()`
call, matching [LUA_API.md](LUA_API.md)'s existing documented contract.

**New regression test**: `testScriptingUnloadPurgesStaleCallbacksAndParkedThreads`
— hot-reloads a script with a live `onUpdate` handler and confirms the
next real `tick()` fires only the still-live replacement script's
handler, then separately unloads a script with a parked `task.wait(1000)`
and confirms a later real `tick()` with a large `dt` does not resume it.

## Lua debugging output in Logger

Already real (Alpha Roadmap Phase 7, confirmed and extended in
[CRASH_TELEMETRY.md](CRASH_TELEMETRY.md)): every gameplay script's
`print()`/`compile error`/`runtime error` output reaches `core::Logger`
under category `"Script"` at the correct level, alongside the matching
`"Console"`/`"Plugin:<name>"` routing for Studio's Debug Console and
scripted plugins. No further work needed here.

## Summary

| Item | Status |
|---|---|
| Lua API reference | Already real (`LUA_API.md`) |
| Lua examples | Built — 3 real, tested example scripts (`examples/lua/`) |
| Lua error formatting | Audited, already real (`debugLevel = 1` gives real file:line context) |
| Lua hot-reload stability | Real dangling-VM bug found and fixed in `Scripting::unload()`, with a real regression test |
| Lua debugging output in Logger | Already real (Phase 7) |
