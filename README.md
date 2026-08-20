# Kronos

**A game engine and creator platform, built from scratch in C++.**
Vulkan renderer, Jolt Physics, a sandboxed embedded Luau runtime, and a
desktop creator tool — all real, all tested, no third-party engine
underneath.

Kronos is currently in **Alpha v1**. The focus right now is
performance, sandbox correctness, and making the first pass smooth for
early testers — not feature-completeness. See [Alpha status](#alpha-status)
below for exactly what that means.

## Core Tech Stack

| Layer | What it is |
|---|---|
| **Language** | C++20, built from source, no engine dependency |
| **Graphics** | Vulkan 1.3 — PBR, cascaded shadow maps, ray-traced shadows/reflections where the GPU supports it, HDR bloom, ACES tonemapping |
| **Physics** | [Jolt Physics](https://github.com/jrouwe/JoltPhysics) — full rigid body simulation, real collision layers, real material friction/restitution |
| **Scripting** | Embedded [Luau](https://luau.org/) — per-script sandboxed VM, real memory/CPU budgets, hot-reload, a `world`/`events`/`network`/`ui` binding surface for gameplay scripts and Studio plugins alike |
| **Networking** | Real-time multiplayer over ENet — client prediction, server reconciliation, delta-compressed snapshots |
| **Build** | CMake ≥ 3.24, most dependencies (EnTT, glm, Jolt, Luau, ENet, Dear ImGui) fetched and built from source via `FetchContent` |

Two real, shippable binaries come out of one build: **`engine_runtime`**
(the client/server) and **`studio`** (the desktop creator tool, same
core, same live scene).

## Quick Start

**Requirements:** CMake ≥ 3.24, a C++20 compiler, Ninja, SDL2 (system
package), a Vulkan 1.3 loader + driver, and `glslc` on `PATH`. Full
details and troubleshooting: [docs/QUICKSTART.md](docs/QUICKSTART.md).

```sh
git clone <this-repo-url>
cd kronos-platform/engine
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j$(nproc)
```

First configure fetches and builds every dependency from source (Luau
and Jolt are the largest) — expect a few minutes. Then run either
binary:

```sh
./build/src/engine_runtime   # boots to Home -> Game Catalogue -> play
./build/src/studio           # the desktop creator tool
```

WASD to move, mouse to look, Space to jump, **E to interact** — the
default showcase world drops you next to a physics prop you can launch
and multiply with a press of E, a real, live demo of the Jolt
integration. Want to try a packaged build instead of compiling?
See [docs/ALPHA_TESTER_GUIDE.md](docs/ALPHA_TESTER_GUIDE.md).

## Roadmap

**Alpha v1** closes out all 9 core platform phases — engine stability,
multiplayer networking, the Luau scripting environment, Studio tooling,
and a moderation/trust-and-safety stack — each verified against the real
codebase, not assumed done. Full account: [docs/ALPHA_ROADMAP.md](docs/ALPHA_ROADMAP.md).

**What's next, post-Alpha:**
- A real in-engine Luau debugger (breakpoints, call stacks) — currently zero scaffolding
- Wider `Logger` migration across remaining call sites
- Expanded gameplay script bindings as real testing surfaces new gaps
- Packaging/distribution polish for non-technical testers

See [docs/ALPHA_COMPLETION_CHECKLIST.md](docs/ALPHA_COMPLETION_CHECKLIST.md)
for the specific, honest list of what's real vs. deliberately deferred.

## Alpha status

The engine, Studio, scripting, multiplayer, and project systems are
real and tested — not a mockup or a stated future plan. A few things
are deliberately scoped down rather than half-built; see
[docs/ALPHA_COMPLETION_CHECKLIST.md](docs/ALPHA_COMPLETION_CHECKLIST.md)
for the current, honest list.

## Contributing / testing

Alpha testers and early contributors — bug reports, feedback, and
custom Luau scripts are all welcome. See [CONTRIBUTING.md](CONTRIBUTING.md).

## Where to go next

| I want to... | Start here |
|---|---|
| Build and run it | [docs/QUICKSTART.md](docs/QUICKSTART.md) |
| Try the packaged Alpha release (no build required) | [docs/ALPHA_TESTER_GUIDE.md](docs/ALPHA_TESTER_GUIDE.md) |
| Fix a build/run problem | [docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md) |
| Write a Studio plugin | [docs/PLUGIN_DEVELOPER_EXPERIENCE.md](docs/PLUGIN_DEVELOPER_EXPERIENCE.md), [docs/PLUGIN_SYSTEM.md](docs/PLUGIN_SYSTEM.md) |
| Write a gameplay script | [docs/LUA_CREATOR_EXPERIENCE.md](docs/LUA_CREATOR_EXPERIENCE.md), [docs/LUA_API.md](docs/LUA_API.md) |
| Understand the multiplayer stack | [docs/NETWORKING_UPGRADE.md](docs/NETWORKING_UPGRADE.md), [docs/MULTIPLAYER_SESSION_UX.md](docs/MULTIPLAYER_SESSION_UX.md) |
| Understand the asset pipeline | [docs/ASSET_PIPELINE.md](docs/ASSET_PIPELINE.md) |
| Package a distributable build | [docs/ALPHA_PACKAGING.md](docs/ALPHA_PACKAGING.md), `scripts/package_alpha.sh` |
| See the full architecture | [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) |

For the full, sprint-by-sprint engineering history (what was built, real
bugs found, and why), see [engine/README.md](engine/README.md).

## License

All rights reserved — see [LICENSE](LICENSE). This source is public for
viewing and evaluation only; copying, modifying, redistributing, or
using it (commercially or otherwise) requires the copyright holder's
prior written permission.

## Repository layout

```
engine/           the engine/Studio source tree, build, and tests (see docs/QUICKSTART.md)
docs/             architecture and subsystem documentation (this is the index above)
games/            real, loadable example games (Game Catalogue content)
templates/plugin/ a real, working starting point for a new Studio plugin
templates/project/a real, loadable default project
examples/lua/     real, tested example gameplay scripts
scripts/          packaging and other repo-level scripts
```
