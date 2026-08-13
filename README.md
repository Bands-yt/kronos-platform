# Kronos — Alpha

Kronos is a reference-implementation game engine and creator platform:
a Vulkan-based 3D engine (`engine_runtime`), a desktop creator tool
(`studio`) built on the same core the runtime ships, real-time
multiplayer, a sandboxed Luau scripting layer for both gameplay scripts
and third-party Studio plugins, and a moderation/trust-and-safety stack —
all built from source, with a 9,500+ check automated test suite backing
it.

This README is the entry point. If you just want to build and run it,
go straight to the [Quickstart](docs/QUICKSTART.md).

## What's here

- **`engine_runtime`** — the shippable client/server binary: window,
  Vulkan renderer (PBR, cascaded shadows, ray-traced shadows/reflections
  where the GPU supports it, HDR bloom/tonemap), Jolt physics, real-time
  multiplayer, a sandboxed Luau VM for gameplay scripts, and a live TNT
  Wars game mode.
- **`studio`** — the desktop creator tool: the same live scene, an
  Explorer/Inspector/Viewport with a real gizmo, an extensible plugin
  system (Luau-scripted third-party plugins included), Undo/Redo, a
  Debug Console with its own Luau VM, and project/scene save-load with
  autosave and crash recovery.
- **A sandboxed Luau scripting layer** shared by gameplay scripts and
  Studio plugins — real `world`/`events`/`network`/`task`/`ui` bindings,
  per-script memory/time budgets, and hot-reload.
- **Real-time multiplayer** — client/server with prediction/reconciliation,
  delta-compressed state sync, a RemoteEvent RPC layer, and a moderation
  stack (chat filtering, reporting, mute/ban, a review queue).

## Where to go next

| I want to... | Start here |
|---|---|
| Build and run it | [docs/QUICKSTART.md](docs/QUICKSTART.md) |
| Fix a build/run problem | [docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md) |
| Write a Studio plugin | [docs/PLUGIN_DEVELOPER_EXPERIENCE.md](docs/PLUGIN_DEVELOPER_EXPERIENCE.md), [docs/PLUGIN_SYSTEM.md](docs/PLUGIN_SYSTEM.md) |
| Write a gameplay script | [docs/LUA_CREATOR_EXPERIENCE.md](docs/LUA_CREATOR_EXPERIENCE.md), [docs/LUA_API.md](docs/LUA_API.md) |
| Understand the multiplayer stack | [docs/NETWORKING_UPGRADE.md](docs/NETWORKING_UPGRADE.md), [docs/MULTIPLAYER_SESSION_UX.md](docs/MULTIPLAYER_SESSION_UX.md) |
| Understand the asset pipeline | [docs/ASSET_PIPELINE.md](docs/ASSET_PIPELINE.md) |
| Package a distributable build | [docs/ALPHA_PACKAGING.md](docs/ALPHA_PACKAGING.md), `scripts/package_alpha.sh` |
| See the full architecture | [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) |
| See what's real vs. what's a deliberate, flagged gap | [docs/ALPHA_COMPLETION_CHECKLIST.md](docs/ALPHA_COMPLETION_CHECKLIST.md) and each section doc it links to |

For the full, sprint-by-sprint engineering history (what was built, real
bugs found, and why), see [engine/README.md](engine/README.md).

## Alpha status

This is an alpha build: the engine, Studio, scripting, multiplayer, and
project systems are real and tested, but a few things are deliberately
scoped down or explicitly flagged rather than half-built — see
[docs/ALPHA_COMPLETION_CHECKLIST.md](docs/ALPHA_COMPLETION_CHECKLIST.md)
for the honest, current list.

## Repository layout

```
engine/           the engine/Studio source tree, build, and tests (see docs/QUICKSTART.md)
docs/             architecture and subsystem documentation (this is the index above)
templates/plugin/ a real, working starting point for a new Studio plugin
templates/project/a real, loadable default project
examples/lua/     real, tested example gameplay scripts
scripts/          packaging and other repo-level scripts
```
