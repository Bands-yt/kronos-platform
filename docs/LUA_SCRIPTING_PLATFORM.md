# Kronos Platform — Phase 7: Lua Scripting Platform

Status of the Alpha Roadmap's Phase 7 ("Lua Scripting Platform") — see
[ALPHA_ROADMAP.md](ALPHA_ROADMAP.md) for the full roadmap and
[LUA_API.md](LUA_API.md) for the complete, real API reference this phase
also produced.

All work below was verified with a full rebuild (`engine_runtime`,
`studio`, `engine_tests`) and the full test suite green after every change
(9410/9410 checks passing, up from 9393 — 17 new tests: real, headless
`ScriptWorldApi` coverage that didn't exist before this phase at all,
plus hot-reload and error-routing tests).

## Audit finding

Unlike several earlier phases, this checklist was mostly real gaps, not
pre-existing work to discover. `core::Scripting` (the sandboxed Luau VM
itself) and `core::ScriptWorldApi`/`core::ScriptNetworkApi` (built in
Phase 4) were real and solid, but:

- **No way to create an entity from Luau at all** — `world.*` had
  `findByName`/`destroy`/getters/setters, but nothing that brought a new
  entity into existence. Flagged explicitly as "real, flagged-missing
  Phase 7 scope" in this session's own Phase 4 work.
- **No screen-space UI binding** — `core::UIRenderer` existed and was
  real (native C++ HUD code already used it for trailer/TNT-Wars
  overlays), but no script could draw to it.
- **Gameplay scripts never hot-reload** — a `Script` component's
  `source` was loaded exactly once; changing it at runtime did nothing.
- **Gameplay script errors were invisible outside a terminal** — real
  compile/runtime errors went to raw `stderr` only, never through the
  real `core::Logger` (Phase 1) or any in-engine surface.
- **No consolidated API reference** existed anywhere, despite every
  binding already carrying real, detailed header-comment documentation.

## 1. Lua API docs

**Built** — [LUA_API.md](LUA_API.md): every real global (`print`,
`engine.log`, `task.*`, `events.*`, `world.*`, `network.*`, `ui.*`) with
its real signature, a per-context availability table (engine_runtime vs.
Studio plugin vs. Studio console — they don't all get the same `world`,
see the table's own note on why), and cross-links to the phase docs that
built each piece.

## 2. Lua entity creation

**Built** — `world.createEntity(name?)` (`core::ScriptWorldApi`): creates
a real, bare entity (`Transform` + optional `Name`). Deliberately no
default `Renderable`/mesh — that needs real Vulkan mesh-building handles
this class has no access to; an honest minimum, not a half-built spawn
system. Paired with `world.setParent()`/`world.unparent()` (real
`core::hierarchy` wiring, Phase 2), so a script can now build and
reparent a small entity hierarchy, not just query pre-existing ones.

**Tests:** `testScriptWorldApiCreateEntityAndHierarchy` — the first real,
headless test coverage `core::ScriptWorldApi` has ever had (previously
only reachable through a full running `engine_runtime`). Real `Physics`
and `RuntimeAnimationPlayer` are both genuinely headless, so this
exercises the *exact* binding engine_runtime registers, not a stand-in.

## 3. Lua networking

Already built in [Phase 4](NETWORKING_UPGRADE.md) — `network.fireServer`/
`fireAllClients`/`onServerEvent`/`onClientEvent`. No new work this phase;
included in the API reference for completeness.

## 4. Lua UI

**Built** — `ui.drawText`/`ui.drawRect` (`core::ScriptUiApi`), a real,
minimal binding to `core::UIRenderer`. Queued, not a direct pass-through:
`UIRenderer::beginFrame()` clears its own batch and is called once per
real frame from `core::Application`'s own HUD code, but scripts run
earlier, during the sim tick — a direct `drawText()` call would just get
wiped out by the later `beginFrame()`. `ScriptUiApi` queues instead;
`Application` flushes the queue right after each real `beginFrame()`
call.

**Honest, real scope boundary** (stated up front, not discovered late):
this only actually renders during the two contexts `Application.cpp`
already drives a `UIRenderer` pass from today — camera-showcase mode and
an active TNT Wars match. A general, always-on HUD pass for every other
scene/mode is a separate, pre-existing architectural gap (`UIRenderer`
itself has no unconditional per-frame drive) that this phase does not
create or claim to fix. This is deliberately **not** the "Plugin UI
access" ask from Phase 5 (a plugin drawing its own ImGui panels) — that
remains out of scope for the same reasons documented there; this is
runtime gameplay HUD drawing, a real, much smaller, already-buildable
thing.

## 5. Lua plugin access

Already satisfied by [Phase 5](PLUGIN_SYSTEM.md): a Studio scripted
plugin already runs in the same real sandboxed `core::Scripting` VM
gameplay scripts do, with real `world`/`network` access. No further work
identified here specific to this phase; the one remaining plugin-UI gap
is tracked in Phase 5's own doc, not duplicated here.

## 6. Lua hot-reload

**Built** — `core::Script` gained `loadedSource` (real bookkeeping, not
authored data): `Application`'s per-tick scan now compares `source`
against `loadedSource` every tick, and a mismatch real-unloads the stale
`scriptId` (`Scripting::unload()`) before real-loading the new source —
the exact same "detect a change, tear down, rebuild fresh" shape
`ScriptedPlugin`'s own `scriptChangedOnDisk()` + `reload()` already
established for Studio's file-backed plugin scripts, applied here to
inline-source gameplay scripts, which have no file to watch.

**Known, accepted gap** (inherited, not introduced): `Scripting::unload()`
doesn't purge `parked_`/`deferredQueue_` entries for the unloaded
script — an outstanding `task.wait()` on the specific script being
hot-reloaded isn't cleaned up. Already a documented "KNOWN GAP" in
`Scripting::unload()`'s own comment; this phase's hot-reload path inherits
it rather than introducing a new one.

**Test:** `testScriptComponentHotReloadsOnSourceChange` — an unchanged
source is never reloaded; a real source change real-triggers a fresh
`loadAndRun()` with a new `ScriptId`, and the new code's effect on the
ECS is verified, not just that a reload "happened."

## 7. Lua error reporting

**Built** — `core::Application` now wires
`scripting_.setOutputCallback()` (previously never set at all for the
gameplay path) to route every `print()`/`engine.log()` call and every
real compile/runtime error through `core::Logger` (Phase 1) — errors at
`Error` level, everything else at `Info`, keyed under category
`"Script"`. Gameplay script problems are now visible in the same
structured, filterable, bounded ring buffer as every other engine
subsystem's logs, including Studio's new Engine Log viewer (Phase 6) when
running in-process — not just raw `stderr` a developer has to be watching
a terminal to see.

**Test:** `testScriptingErrorsRouteToLoggerAtCorrectLevel` — a plain
`print()` lands at Info, a real compile error and a real runtime error
both land at Error, all under the real `Logger` singleton.

## Summary

| Item | Status |
|---|---|
| Lua API docs | Built — [LUA_API.md](LUA_API.md) |
| Lua entity creation | Built — `world.createEntity`/`setParent`/`unparent`, first-ever headless `ScriptWorldApi` tests |
| Lua networking | Already real (Phase 4) |
| Lua UI | Built — `ui.drawText`/`drawRect`, honest scope boundary stated (2 real contexts only) |
| Lua plugin access | Already real (Phase 5) |
| Lua hot-reload | Built — gameplay `Script` source-change detection + reload |
| Lua error reporting | Built — routed through `core::Logger` |
