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
`engine_runtime` boots into a Home Screen with three real buttons:
**Launch Studio** (opens the editor as a separate process), **Game
Catalogue** (browse and play real local games — see below), and
**Sessions** (join a real LAN multiplayer session someone else is
hosting). Once in a game the controls work like Roblox:

| Input | What it does |
|---|---|
| WASD | Move (the character turns to face where it walks) |
| Space | Jump |
| Left Ctrl (hold) | Run |
| Right mouse (hold) and drag | Look around; the cursor hides while you drag and comes back where it was |
| Mouse wheel | Zoom in and out |
| Shift | Shift lock on/off: the mouse turns the camera and the character faces where you look, with the camera over the right shoulder |
| E | Interact |
| Escape | Pause menu |

The camera stops in front of walls and floors instead of going through them.

### Play in Studio

**Play** (Home tab) drops your avatar into the scene, like Roblox's Play
button: same body as on the Avatar page, same controls as the table
above, with physics, scripts and sound running. While playing, the
Explorer lists your character under your display name, and Ctrl is Run
(so editor shortcuts like Ctrl+S and Ctrl+Z wait until you stop).
**Stop** removes the avatar, puts every part back where it was and
returns the editor camera.

**Spawn point:** name a part `SpawnLocation` (or `SpawnPoint`) and players
appear just above it, in Studio and in the Player. Without one they appear
at (0, 3, -6).

### Game Catalogue and the `games/` folder

The Game Catalogue browses real local games under `games/` (a sibling
of the source repo's `engine/`) — each is its own folder,
`games/<Name>/game.gamemanifest` (name, description, genre tags,
manually-authored effort score) plus either a real `project.project` +
`.scene` pair `engine_runtime` genuinely loads at runtime
(`runtime::loadGame()`), or a `CLIFLAG` pointing at one of the launch
modes in the table below for the still-hardcoded rich modes (TNT
Wars/Mining Sim/House Demo). Two real example games ship in the repo:
`games/DefaultWorld` (the original bring-up scene) and `games/SkyGarden`
(a small floating sandbox) — copy either folder as a starting point for
a new one. Built-in modes such as Broken Bones are listed with a
`CLIFLAG` manifest so they appear and count plays like any other game.

### My Games

Like Roblox's Create page, every game you save from Studio shows up in
the Player under **Create → My Games**:

- **File → Save Game** (Ctrl+S) asks for a name the first time and saves
  the game into its own folder in `Documents/Kronos/<Name>/`
  (`default.scene`, `project.project` and `game.gamemanifest`). Later
  Ctrl+S saves go back to the same game.
- Projects saved anywhere else are remembered in a small list
  (`~/.local/share/kronos/my_games.list`, or
  `%LOCALAPPDATA%\Kronos\my_games.list` on Windows), so they still show up.
- Clicking a game opens its page with **Play** and **Edit in Studio**.
  **File → Open My Game** in Studio lists the same games.
- `KRONOS_MY_GAMES_DIR` and `KRONOS_MY_GAMES_REGISTRY` override both
  locations (used by tests).

### Parts are solid

Parts inserted from Studio's **Part** button are solid and anchored, like
Roblox parts: they get a static collider that matches their shape, and it
follows the part when you resize it. Edit the collider by hand in the
Inspector's Physics section and Studio leaves it alone after that.

`engine_runtime` also accepts these launch modes:

| Flag | What it does |
|---|---|
| `--server [port]` | Host a real multiplayer session |
| `--client <address> [port]` | Connect to one |
| `--stress <playerCount>` | Server-only: connect that many real synthetic client players, for load testing |
| `--tntwars [map]` | Launch directly into a live, playable TNT Wars match (`map` is one of `sky`/`volcano`/`underwater`/`trenches`, defaults to `trenches`) |
| `--miningsim` | Launch the interactive Mining Sim prototype scene |
| `--house-demo` | Launch a real built house (working door, 2 windows, kitchen, fireplace) on rolling-hill terrain |
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
client/server networking over real loopback ENet. Real shipped fixtures
(templates/examples/games/assets) resolve correctly regardless of the
current working directory or whether you're running the dev build or a
packaged alpha zip — see `core/ResourcePaths.hpp`.

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
