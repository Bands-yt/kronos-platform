# Kronos Platform — Phase 5: Plugin System Expansion

Status of the Alpha Roadmap's Phase 3 ("Plugin System Expansion") — see
[ALPHA_ROADMAP.md](ALPHA_ROADMAP.md) for the full roadmap.

All work below was verified with a full rebuild (`engine_runtime`,
`studio`, `engine_tests`) and the full test suite green after every change
(9387/9387 checks passing, up from 9377 — 10 new tests, including the two
that caught a real, serious bug this phase introduced and fixed before it
shipped — see §3 below).

## Audit finding

The roadmap's checklist — "Plugin loader / sandboxing / permissions / UI
access / networking access / asset access / lifecycle events" — again
reads as if starting from nothing. An audit of `studio/` found the loader,
sandboxing, and permission model already real: `studio::PluginManager`
hosts both first-party native `IStudioPlugin`s and third-party
`studio::plugins::ScriptedPlugin`s through the identical
`registerPlugin()` path (no separate "untrusted plugin" code path);
`ScriptedPlugin` runs a manifest-declared entry script in a real,
sandboxed `core::Scripting` VM — the *exact same* per-script memory/time
budget and interrupt watchdog gameplay scripts get, not a separate,
weaker plugin sandbox. All of this predates this session.

Three real, concrete gaps, matching the roadmap's own remaining bullets:
no networking access, no lifecycle events beyond implicit load/update,
and no UI access (the last one a deliberate non-goal, not attempted —
see §4).

## 1. Plugin networking access

**Wired** — `ScriptedPlugin` now constructs a `core::ScriptNetworkApi`
(Phase 4's own Luau `network` binding) wrapping Studio's real, live
`net::NetworkSession` (`StudioApp::networkSession_`, already used by
`NetworkOverlayPlugin`/`ModerationPanel`), registered alongside `world`
in the same `setBindingsHook()`. `ScriptedPlugin::load()` gained a third
parameter (`net::NetworkSession&`), threaded through
`PluginBrowserPlugin`'s constructor (now takes the session too) from
`StudioApp.cpp`'s registration call — one real call site, updated.

A scripted plugin can now do exactly what a gameplay script can:
`network.fireServer(...)`, `network.onServerEvent(...)`,
`network.fireAllClients(...)`, `network.onClientEvent(...)`.

## 2. Plugin lifecycle events — `onUnload`

**Built** — `events.onUnload(fn)` (`core::Scripting`), fired by
`Scripting::shutdown()`. `onLoad` was already implicit (a script's own
top-level chunk running once *is* onLoad) and `onUpdate` already existed
(`events.onUpdate`, ticked every frame `ScriptedPlugin::update()` calls
`scripting_.tick(dt)`) — `onUnload` was the one genuinely missing.
`ScriptedPlugin::reload()` (→ `loadInternal()` → `scripting_.shutdown()`
→ `scripting_.initialize()`) now gives a plugin a real chance to clean up
— stop a running effect, release a resource — before its old VM state is
discarded, instead of it just vanishing.

## 3. A real bug, caught by testing

Firing `onUnload` from `Scripting::shutdown()` seemed straightforward
until `testScriptedPluginRealNetworkAccessAndOnUnloadFiresOnReload` — not
the reload path it was written to check, but the plugin object's own
teardown at the test's end — crashed with `free(): double free detected
in tcache 2`, and a *later*, unrelated test then failed to compile a
script that had worked in every prior run. Classic heap-corruption
symptoms: a bad write earlier corrupting an unrelated allocation later.

`gdb` traced it exactly: `~ScriptedPlugin()` destroys `scripting_`
(`core::Scripting`), whose destructor called `shutdown()`, which now
fired `events.onUnload()` — invoking a registered Lua callback that
called `print(...)`, which called `ScriptedPlugin::appendLine()`, which
called `outputLog_.push_back(...)` — on an `outputLog_` that **C++ had
already destroyed**. `outputLog_` is declared *before* `scripting_` in
`ScriptedPlugin`, and C++ destroys members in reverse declaration order:
`outputLog_` was gone by the time `scripting_`'s own destructor ran a
callback that touched it. A real use-after-destruction bug, not a test
artifact — and the same hazard would have existed for *any* class
embedding a `core::Scripting` member alongside other members its
callbacks can reach (`core::Application` included, via `network.*`
touching `networkSession_`).

**Fixed at the root**, not by reordering members in every affected class
(fragile, and easy to get wrong again the next time a class like this
gains a new member — as this exact bug just demonstrated). `Scripting`
now has a real, deliberate asymmetry:

- `shutdown()` — fires `onUnload` first, then tears down. Always called
  *explicitly*, by an owner that still has its own full object alive at
  the call site (`ScriptedPlugin::loadInternal()`'s reload path).
- `~Scripting()` — tears down directly (`closeAllScripts()`), *never*
  fires `onUnload`. C++ destructor ordering cannot guarantee sibling
  members are still alive, so this path must not invoke arbitrary
  callbacks into them.

This means `onUnload` fires reliably on every real reload (the case this
phase actually cares about — a plugin being swapped out while Studio
keeps running) and never fires during final process/object teardown,
where there's no meaningful "clean up into" state left anyway.

**Tests:** `testScriptingOnUnloadFiresOnShutdown` (headless, `core::
Scripting`-only: registering a handler doesn't fire it; an explicit
`shutdown()` does) and `testScriptedPluginRealNetworkAccessAndOnUnloadFiresOnReload`
(a real plugin script calling `network.fireServer()` and
`events.onUnload()`, verifying both the network global works and
`reload()` fires the old VM's `onUnload` handler) — the second one is
what caught the bug above, both now pass clean.

## 4. Plugin UI access and asset access — deliberately out of scope

**UI access**: `ScriptedPlugin`'s own header comment already named this
correctly before this phase — a script drawing its own ImGui widgets
needs a whole declarative-UI-from-Luau surface (widget tree description,
layout, event wiring back to script callbacks), a real, separate,
substantial feature. Half-building it (say, three hardcoded widget types)
would be worse than the honestly-absent seam that's there now. Not
attempted.

**Asset access**: the roadmap's own Phase 8 ("Asset Pipeline") is where
an asset *registry* gets built — building plugin-facing asset access
before that registry exists would mean inventing an ad-hoc API now and
likely replacing it once the real registry lands. Deliberately deferred
to land naturally alongside Phase 8, not skipped.

## Summary

| Item | Status |
|---|---|
| Plugin loader | Already real (pre-existing) |
| Plugin sandboxing | Already real (pre-existing, same budget/watchdog as gameplay scripts) |
| Plugin permissions | Already real (pre-existing — no separate first/third-party code path) |
| Plugin UI access | Deliberately out of scope — needs a real declarative-UI-from-Luau feature this phase doesn't half-build |
| Plugin networking access | Built — real `network` table, same binding gameplay scripts get |
| Plugin asset access | Deliberately deferred to Phase 8 (asset registry doesn't exist yet) |
| Plugin lifecycle events (onLoad/onUnload/onUpdate) | onLoad/onUpdate already real; onUnload built — and a real use-after-destruction bug it introduced was caught by testing and fixed at the root |
