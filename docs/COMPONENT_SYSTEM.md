# Kronos Platform — Phase 3: Component System

Status of the Alpha Roadmap's Phase 5 ("Scene + Entity System") component
checklist — see [ALPHA_ROADMAP.md](ALPHA_ROADMAP.md) for the full roadmap
and [SCENE_SYSTEM.md](SCENE_SYSTEM.md) for Phase 2 (the hierarchy half of
that same roadmap section, done first).

All work below was verified with a full rebuild (`engine_runtime`,
`studio`, `engine_tests`) and the full test suite green after every change
(9352/9352 checks passing, up from 9346 — 6 new tests: 4 for the `Light`
component's save/capture round-trip, 2 for the `Script` component's real
auto-run wiring).

## Scope

The roadmap's checklist is "Component system (Transform, Mesh, Light,
Script, Network)". An audit against the live codebase found this list was
**not** a blank slate:

| Component | Status before this phase |
|---|---|
| Transform | Already real — every entity's core position/rotation/scale |
| Mesh | Already real, as `Renderable` + `MeshSource` |
| Light | Did not exist as a component (`SceneLighting::pointLights` was manually authored per-scene, never entity-driven) |
| Script | Did not exist at all — `core::Scripting` could load/run Lua, but nothing associated a script with an entity |
| Network | Already real and fully wired, as `net::NetworkIdentity` |

So this phase's real work was exactly two components: `Light` and
`Script`. `Network` is documented here rather than silently skipped, so
the roadmap checklist item has an honest, verifiable answer instead of an
unexplained gap.

## 1. Light component

**Built — `core::Light`** (`core/Components.hpp`): `enabled`, `color`,
`intensity`, `radius`. Position is deliberately not duplicated onto it —
it reads the entity's own `Transform`, same convention as `Renderable`.

**Wired into the renderer** (`core/Renderer.cpp`, the same point-light
UBO-fill block `SceneLighting::pointLights` already used): every live
`Light`+`Transform` entity contributes a real slot, filling whatever
budget the manually-authored `SceneLighting::pointLights` entries didn't
already use, against the same `kMaxPointLights = 4` cap (`SceneTypes.hpp`)
— extras silently dropped, matching that array's own established
convention, not a new failure mode. World position goes through
`hierarchy::computeWorldMatrix()` (Phase 2), not raw `Transform::position`,
so a `Light` parented under a moving rig tracks it correctly.

**Persisted** — `core::SceneFile`/`SceneEntityRecord` gained `hasLight`/
`light`, serialized as an optional `LIGHT <enabled> <r> <g> <b>
<intensity> <radius>` line, and `studio::SceneManager` captures/rebuilds
it on save/load — the same optional-field convention every other
component in that file already follows.

**Honest verification gap:** the renderer-side UBO fill is inline logic
inside a Vulkan draw function (`drawSceneIntoImpl`), which this codebase's
test suite deliberately never links against a window/GPU (see
`tests/test_main.cpp`'s own header comment) — matching the same ceiling
already flagged for the glass/water shader and the Explorer tree's ImGui
code. Verified by a full clean compile and code review, not a captured
frame, this pass.

## 2. Script component

**Built — `core::Script`** (`core/Components.hpp`): `source` (inline Luau
text — no asset-provenance tracking for script *files* exists yet, the
same real gap `SceneFile.hpp` already flags for textures), `autoRun`
(default true), `scriptId` (`kInvalidScript` until loaded, never reloaded
after).

**Wired into `core::Scripting`** (`core/Application.cpp`'s pre-tick hook,
right alongside the other general per-tick systems — `particleSystem_`,
`animationPlayer_`): every tick, any live `Script` with `autoRun` and no
`scriptId` yet gets real-loaded via `scripting_.loadAndRun()`, keyed by
the entity's own `Name` (falling back to `"Script"` if unnamed) so its
compile/runtime errors are traceable to a specific entity. Deliberately
placed in `Application`'s hook, not inside `GameLoop` itself — `GameLoop`'s
own header comment states it deliberately doesn't grow gameplay logic,
and `setPreTickHook()` is exactly the seam it offers for this.

A script that needs to act on its own entity uses `world.findByName()`
against its own `Name`, the same mechanism every other Luau binding in
this engine already requires — there is no `self`/`script.Parent`-style
binding, since that would need the Instance/DataModel layer
`Scripting.hpp`'s own class comment already says doesn't exist yet
(deliberately not faked).

**Not persisted to `SceneFile`, on purpose:** the file format is a real
"one field per line" text convention every existing field relies on
(`out << "KEY value value...\n"`), and Luau source is real, legitimate
multi-line text — serializing it under that convention today would either
silently corrupt on the first embedded newline or need a real
length-prefixed/escaped framing scheme this phase didn't build. Rather
than ship a `Script` that appears to round-trip but corrupts on any
non-trivial script, this is left as an explicit, tracked gap, not a
silent one.

**Tests (2 new, `testScriptComponentAutoRun`):** replicates the exact
scan `Application.cpp` runs, driven by a real headless `core::Scripting`
instance (no window/GPU needed — `Scripting` has zero graphics
dependency, confirmed by `ScriptedPlugin`'s own existing headless tests)
— verifies a freshly-attached `Script` starts unloaded, the scan loads it
and records a real `ScriptId`, the script's own `world.setPosition()` call
reaches its own entity via `world.findByName(Name)`, and a second scan
pass never reloads an already-loaded script.

## 3. Network component — already real, no new work

**`net::NetworkIdentity`** (`net/NetworkIdentity.hpp`, built in an earlier
sprint) already is exactly this: `networkId` (server-assigned, monotonic,
never reused within a process's lifetime — the real link between a local
`EntityId` and the network-portable id every snapshot/RPC uses),
`ownerId` (`kInvalidPlayer` = server-owned world state), and
`isLocallyControlled` (gates client-side prediction). It's a real,
attached ECS component (`ecs.addComponent<NetworkIdentity>(...)`, queried
via `ecs.view<NetworkIdentity, Transform>()`), used throughout
`NetworkSession`, `ClientPrediction`, `RemoteEntityInterpolation`, and
`Serialization`, with its own dedicated tests
(`testNetworkIdentityDefaultsMatchDocumentedContract`,
`testNetworkIdentityAsRealEcsComponent`) plus broad exercise through the
existing networking integration tests.

Building a second, parallel `core::Network` marker component would create
two competing "is this entity networked" concepts in the same engine —
actively worse than the roadmap checklist item being unchecked. This
phase's real work here was confirming that, not writing new code.

## Summary

| Item | Status |
|---|---|
| Transform | Already real (pre-existing) |
| Mesh (`Renderable`/`MeshSource`) | Already real (pre-existing) |
| `Light` | Built — component, renderer wiring, scene-file persistence, 4 tests |
| `Script` | Built — component, `core::Scripting` wiring, 2 tests. Scene-file persistence explicitly not attempted (multi-line source vs. the file format's one-field-per-line convention) |
| `Network` (`net::NetworkIdentity`) | Confirmed already real, tested, and fully wired — no new work |

The roadmap's Phase 5 "Component system" checklist is now honestly
complete: every listed component is real, and the one gap this phase
didn't close (`Script` scene-file persistence) is called out rather than
silently claimed.
