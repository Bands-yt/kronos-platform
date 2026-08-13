# Kronos Platform — Phase 6: Tooling Layer

Status of the Alpha Roadmap's Phase 6 ("Tooling Layer (Editor Mode)") —
see [ALPHA_ROADMAP.md](ALPHA_ROADMAP.md) for the full roadmap.

All work below was verified with a full rebuild (`engine_runtime`,
`studio`, `engine_tests`) and the full test suite green after every change
(9393/9393 checks passing, up from 9387 — 6 new tests for the one new
piece of pure, headlessly-testable logic this phase added,
`spawnPointLightAuthoring()`; the two new ImGui-only panel sections have
no pure logic to extract, matching this codebase's existing convention
for `draw()`-shaped code).

## Audit finding

Another checklist that reads as a blank slate but mostly wasn't. An audit
of `studio/` found real, working implementations of five of the seven
bullets already in place: dockable panels (a real `ImGui::DockBuilder`-
based layout, `StudioApp.cpp`), a material editor (`MaterialPlugin`), a
particle editor (`ParticleEditorPlugin`), a networking monitor
(`NetworkOverlayPlugin` — real connected-peer count and `NetworkStats`,
not placeholder numbers), and a plugin manager (`PluginManager`/
`PluginBrowserPlugin`, just extended further in Phase 5).

Two real gaps, both concrete and both directly traceable to earlier
phases' own work:

1. **"Console + logs"**: a Luau REPL console (`DebugConsolePanel`)
   existed, but nothing surfaced `core::Logger`'s ring buffer — the
   structured logging layer built in Phase 1, whose own header comment
   already named this exact panel as where it should land ("a bounded
   thread-safe ring buffer a future Studio 'Console' panel can read back
   from directly instead of re-parsing stdout").
2. **"Lighting editor"**: `LightingToolsPlugin` existed and is real, but
   edits only scene-wide `SceneLighting` (sun/ambient/fog/sky) — there
   was no way to create or edit the entity-driven `core::Light` component
   built in Phase 3, anywhere in Studio.

## 1. Console + logs

**Built** — `DebugConsolePanel` gained a real tab bar: the existing REPL
becomes the "REPL" tab, unchanged; a new "Engine Log" tab shows a live,
per-frame snapshot of `core::Logger::instance().recentEntries()`,
color-coded by level (matching `CreatorConsolePlugin`'s own tips/
warnings/errors severity-coloring convention), with a checkbox per level
(Debug/Info/Warn/Error) to filter and a Clear button wired to
`Logger::clearRingBuffer()`.

## 2. Lighting editor — entity-driven point lights

**Built — `studio::spawnPointLightAuthoring()`**
(`studio/CreatorToolsSpawning.{hpp,cpp}`, the same real, headlessly-
tested spawn-function module `spawnPropAuthoring`/
`spawnTeleportPadAuthoring`/`spawnNavMarkerAuthoring` already live in):
creates a `Transform` + a small emissive `Renderable`/`MeshSource` (so
the light is actually visible/selectable in the Viewport — a bare
`Light` component has no mesh) + a real `core::Light`. A "Spawn Point
Light" button in `CreatorToolsPlugin` (alongside the existing prop/
teleport-pad/nav-marker buttons) calls it at the current spawn position.

**Built — `InspectorPanel::drawLightSection()`**: once a spawned (or any)
`Light`-bearing entity is selected, a real "Light" section appears
alongside the existing Transform/Renderable/Physics/Navigation/OreNode
sections — enabled toggle, color, intensity, radius — editing the exact
fields `Renderer.cpp`'s point-light UBO fill (Phase 3) reads every frame.

**Test:** `testSpawnPointLightAuthoringCreatesRealLightAndVisual` — real
`Light` attached and enabled by default, a real visible/emissive
`Renderable`, correct spawn position, correct disambiguated name.

## Summary

| Item | Status |
|---|---|
| Dockable panels | Already real (pre-existing), confirmed |
| Material editor | Already real (pre-existing), confirmed |
| Particle editor | Already real (pre-existing), confirmed |
| Lighting editor | Scene-wide editing already real; entity-driven `Light` create + edit built this phase |
| Networking monitor | Already real (pre-existing), confirmed |
| Plugin manager | Already real, extended in Phase 5 |
| Console + logs | REPL already real; `core::Logger` ring-buffer view built this phase |

Both real gaps this phase found are now closed, tying Phase 1's Logger
and Phase 3's Light component into Studio's own UI rather than leaving
either as C++-only infrastructure nothing surfaces.
