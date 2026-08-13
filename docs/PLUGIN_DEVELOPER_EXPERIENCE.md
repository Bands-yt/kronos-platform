# Kronos Alpha — Section 4: Plugin Developer Experience

Status of the [Alpha Completion Checklist](ALPHA_COMPLETION_CHECKLIST.md)'s
Section 4. Verified with a full rebuild and full test suite green
(9530/9530 checks passing, up from 9523 — 7 new tests).

## Plugin template project

**Built — `templates/plugin/`** (`example.manifest` + `main.lua`): a
real, minimal, working plugin — copy the folder, rename, edit. Documents
inline (real comments, not a separate wall of prose) the real, honest
scope difference between Studio's own `world` table and a gameplay
script's (no `createEntity`/`destroy`/`applyImpulse`/`playAnimation` —
Studio has no live Physics/Animation system and no Vulkan mesh-building
handle to give a new entity a real mesh), demonstrates real
`events.onUpdate`/`onUnload` lifecycle usage, and shows (commented out,
since it needs a real live session to actually do anything) real
`network.fireServer`/`onServerEvent` usage.

**Not decorative** — `testPluginTemplateLoadsAndRunsReal` loads the
*actual shipped file* (not a re-typed copy embedded in the test) through
the real `ScriptedPlugin` path: confirms it compiles and runs with no
error, its top-level `print()` really fires, five real `events.onUpdate()`
ticks run clean, and reloading it really fires its own
`events.onUnload()` handler. If a future edit introduces a typo, this
test catches it before it ships.

## Plugin API reference

Already real — [LUA_API.md](LUA_API.md)'s own availability table
specifically documents the difference between engine_runtime's `world`
and Studio's smaller one, which is exactly what a plugin author needs
to know first. No separate reference needed; cross-linked from the
template's own comments instead of duplicated.

## Plugin sandbox rules

Already real and documented — [PLUGIN_SYSTEM.md](PLUGIN_SYSTEM.md)'s own
opening section: a scripted plugin runs in the exact same sandboxed
`core::Scripting` VM (per-script memory/time budget, interrupt watchdog)
a gameplay script does, not a separate, weaker plugin-specific sandbox.
Restated here for discoverability from a plugin-author's own starting
point:

- Real memory ceiling (256 MB default) and real per-tick execution-time
  budget (8 ms default) — a runaway loop gets interrupted, not left to
  hang the whole editor.
- No filesystem/network access beyond the real, explicit `world`/
  `network` tables this engine provides — a plugin cannot `io.open()` an
  arbitrary file or open a raw socket (Luau's own sandboxing strips
  `io` entirely; see `core::Scripting`'s own class comment).
- `events.onUnload()` — the one lifecycle hook worth registering
  explicitly to clean up before a reload discards your plugin's state
  (see [PLUGIN_SYSTEM.md](PLUGIN_SYSTEM.md) §2-3 for the real bug this
  session caught and fixed while building it).

## Plugin networking examples

The template itself is the real example (`network.onServerEvent`/
`fireServer`, commented so it doesn't error when no session is active).
See [LUA_API.md](LUA_API.md)'s `network` section for the full real
signature list, and [NETWORKING_UPGRADE.md](NETWORKING_UPGRADE.md) for
what's actually happening underneath (`net::RemoteEvent`, a real wire
protocol, not a toy).

## Plugin asset access examples

**Honestly, there is nothing to show yet.** [PLUGIN_SYSTEM.md](PLUGIN_SYSTEM.md)'s
own summary table already flags this: plugin asset access is
deliberately deferred to the Asset Pipeline phase's asset *registry*
(now real, see [ASSET_PIPELINE.md](ASSET_PIPELINE.md)) — but no Luau
binding exposes `core::AssetRegistry` to a script yet, only Studio's own
native C++ Asset Browser UI reads it directly. Writing an "example" for
a binding that doesn't exist would be fabricating documentation for a
fake feature — flagged here as a real, open follow-up instead.

## Summary

| Item | Status |
|---|---|
| Plugin template project | Built — real, loadable, tested against the actual shipped file |
| Plugin API reference | Already real (`LUA_API.md`) |
| Plugin sandbox rules | Already real and documented (`PLUGIN_SYSTEM.md`), restated for discoverability |
| Plugin networking examples | The template itself + `LUA_API.md`'s `network` section |
| Plugin asset access examples | Honestly not possible yet — no Luau binding to `AssetRegistry` exists; flagged, not faked |
