# Quickstart

Build, run, and test Kronos from a fresh checkout. For problems along the
way, see [TROUBLESHOOTING.md](TROUBLESHOOTING.md).

## Requirements

- CMake >= 3.24 and a C++20 compiler (tested with GCC 16 and Clang 22 on
  Linux)
- Ninja (or any CMake generator)
- SDL2 (system package — `pacman -S sdl2` / `apt install libsdl2-dev` /
  etc. — must provide `SDL2Config.cmake` or `sdl2-config`)
- A Vulkan 1.3 loader (`libvulkan.so.1` / `vulkan-1.dll`) and driver
- **`glslc`** (part of the Vulkan SDK / `shaderc`) on `PATH` — required,
  the build compiles every shader to SPIR-V at build time
- Network access the first time you configure — every other dependency
  (EnTT, glm, Jolt, Luau, ENet, Dear ImGui, VMA, ImGuizmo, entt, nlohmann_json)
  is fetched via CMake `FetchContent`, pinned to specific tags/commits in
  `engine/cmake/Dependencies.cmake`

## Build

```sh
cd engine
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j$(nproc)
```

First configure fetches and builds every dependency from source (Luau
and Jolt are the largest); expect a few minutes. Subsequent builds are
incremental.

This produces two executables under `engine/build/src/`:

- **`engine_runtime`** — the shippable client/server binary.
- **`studio`** — the desktop creator tool.

Build options (pass as `-D<OPTION>=ON/OFF` at configure time):

| Option | Default | Meaning |
|---|---|---|
| `ENGINE_BUILD_RUNTIME` | `ON` | Build `engine_runtime` |
| `ENGINE_BUILD_STUDIO` | `ON` | Build `studio` (pulls in Dear ImGui) |
| `ENGINE_BUILD_TESTS` | `ON` | Build `engine_tests` and register it with `ctest` |

## Run

```sh
./build/src/engine_runtime
./build/src/studio
```

Both need a real Vulkan-capable GPU and driver. `studio` opens with a
docked Explorer/Inspector/Viewport, a live scene you can fly through and
edit, and a File menu with Save/Load Scene and Save/Open Project.
`engine_runtime` boots straight into a playable scene (WASD to move,
mouse to look, Space to jump, E to interact — mouse is captured on
launch; Alt+Tab or close the window to release it).

`engine_runtime` also accepts these launch modes:

| Flag | What it does |
|---|---|
| `--server [port]` | Host a real multiplayer session |
| `--client <address> [port]` | Connect to one |
| `--stress <playerCount>` | Server-only: connect that many real synthetic client players, for load testing |
| `--tntwars [map]` | Launch directly into a live, playable TNT Wars match (`map` is one of `sky`/`volcano`/`underwater`/`trenches`, defaults to `trenches`) |
| `--miningsim` | Launch the interactive Mining Sim prototype scene |
| `--render-showcase` | A scripted-camera rendering showcase, no gameplay |
| `--trailer [script] [outputDir]` | Batch cinematic capture mode (writes frames, doesn't open an interactive window) |

## Test

```sh
./build/tests/engine_tests
# or:
cd build && ctest --output-on-failure
```

No GPU, window, or live Vulkan device needed — the whole suite is
assertion-based checks over pure logic and real, in-process
client/server networking over real loopback ENet. Run it from
`engine/build/` (some tests reference fixtures via paths relative to
that directory, e.g. `../../templates/plugin/example.manifest`).

## Try the plugin and scripting starting points

- Copy `templates/plugin/` to start a new Studio plugin — see
  [PLUGIN_DEVELOPER_EXPERIENCE.md](PLUGIN_DEVELOPER_EXPERIENCE.md).
- Look at `examples/lua/` for real, working gameplay-script examples —
  see [LUA_CREATOR_EXPERIENCE.md](LUA_CREATOR_EXPERIENCE.md).
- Copy `templates/project/` to start a new project with a real, loadable
  starter scene already in it.

## Package a distributable build

```sh
scripts/package_alpha.sh
```

Assembles a real, relocatable copy of both binaries plus their shaders,
assets, and templates into `dist/kronos-alpha/` — see
[ALPHA_PACKAGING.md](ALPHA_PACKAGING.md).
