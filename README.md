# Kronos

**Make games and play them, on a game engine built from scratch.**

Kronos is a game platform like Roblox, with a fresh coat of paint: a
**Player** for playing games and dressing up your avatar, and **Kronos
Studio** for building your own games with parts, Luau scripts, physics
and sound. Under the hood it's a C++ engine with a Vulkan renderer, Jolt
Physics and Luau scripting. There's no third-party engine underneath.

> Kronos is in **public beta**. Expect rough edges, and please
> [report anything that breaks](https://github.com/Bands-yt/kronos-platform/issues/new/choose).

![Playing as your avatar in Kronos Studio](docs/images/studio-play.png)

## Get it

Download the latest release from the
[Releases page](https://github.com/Bands-yt/kronos-platform/releases):

| Your computer | Download | Then |
|---|---|---|
| Windows 10/11 | `KronosSetup.exe` | Run it, then open **Kronos Player** or **Kronos Studio** from the Start menu |
| Windows (no installer) | `kronos-<version>-windows-x64.zip` | Unzip it and run `engine_runtime.exe` (Player) or `studio.exe` (Studio). Keep the folder together |
| Linux | `kronos-<version>-linux-x64.tar.gz` | Extract it and run `./engine_runtime` (Player) or `./studio` (Studio) |

You need a graphics card with **Vulkan** support. Almost every card from
the last ten years has it; update your graphics driver if Kronos won't
start. If Windows says `VCRUNTIME140.dll was not found`, install the
[Microsoft Visual C++ Redistributable](https://aka.ms/vs/17/release/vc_redist.x64.exe).

## Your first game in five minutes

1. Open **Kronos Studio**. A starter world opens with a ground and a box.
2. Click **Part** (Home tab) to add blocks. Drag the arrows to move them;
   **Scale** and **Rotate** are next to Move.
3. Name one part `SpawnLocation` in the Inspector. That's where players
   appear.
4. Press **Play**. Your avatar drops into the world. Walk around, jump on
   your parts, then press **Stop** and keep building.
5. Press **Ctrl+S** and give your game a name. It now shows up in the
   Player under **Create → My Games**, ready to play.

Want things to move? Select a part, open the **Script Editor** and write
some Luau. The [Lua API](docs/LUA_API.md) lists everything scripts can do.

## Controls

| Input | What it does |
|---|---|
| WASD | Move |
| Space | Jump |
| Left Ctrl (hold) | Run |
| Right mouse (hold) and drag | Look around |
| Mouse wheel | Zoom |
| Shift | Shift lock: the character faces where you look |
| Escape | Pause menu (Player) |

In Studio, hold the right mouse button and use WASD to fly the editor
camera, and scroll to zoom.

## Found a bug? Have an idea?

- [Report a bug](https://github.com/Bands-yt/kronos-platform/issues/new?template=bug_report.yml).
  If Kronos crashed, attach the `crash_report_<date>.txt` file from the
  folder it ran in.
- [Suggest an idea](https://github.com/Bands-yt/kronos-platform/issues/new?template=idea.yml).

## Build it yourself

**Requirements:** CMake ≥ 3.24, a C++20 compiler, Ninja, SDL2, a Vulkan
loader and driver, and `glslc` on `PATH`. Details and troubleshooting:
[docs/QUICKSTART.md](docs/QUICKSTART.md).

```sh
git clone https://github.com/Bands-yt/kronos-platform.git
cd kronos-platform/engine
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j$(nproc)
./build/src/engine_runtime   # the Player
./build/src/kronos_studio    # Kronos Studio
```

The first build downloads and compiles every dependency (Luau and Jolt
are the biggest), so it takes a few minutes.

## Learn more

| I want to... | Read |
|---|---|
| Build and run from source | [docs/QUICKSTART.md](docs/QUICKSTART.md) |
| Fix a problem starting Kronos | [docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md) |
| Script my game | [docs/LUA_CREATOR_EXPERIENCE.md](docs/LUA_CREATOR_EXPERIENCE.md), [docs/LUA_API.md](docs/LUA_API.md) |
| Add music and sound | [docs/AUDIO_MIXER.md](docs/AUDIO_MIXER.md) |
| Write a Studio plugin | [docs/PLUGIN_API.md](docs/PLUGIN_API.md) |
| See what's coming next | [docs/ROADMAP.md](docs/ROADMAP.md) |
| Understand how the engine fits together | [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) |

## License

All rights reserved; see [LICENSE](LICENSE). The source is public for
viewing and evaluation only. Copying, modifying, redistributing or using
it (commercially or otherwise) needs the copyright holder's written
permission.

## Repository layout

```
engine/      engine, Player and Studio source, build and tests
docs/        documentation (see "Learn more" above)
games/       example games shown in the Player
templates/   the starter project and a starter plugin
examples/    example Luau scripts
installer/   Windows installer
scripts/     packaging and helper scripts
```
