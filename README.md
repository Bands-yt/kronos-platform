# Kronos

**Make games and play them, on a game engine built from scratch.**

Kronos is a game platform like Roblox, with a fresh coat of paint: a
**Player** for playing games and dressing up your avatar, and **Kronos
Studio** for building your own games with parts, Luau scripts, physics
and sound. Under the hood it's a C++ engine with a Vulkan renderer, Jolt
Physics and Luau scripting. There's no third-party engine underneath.

Kronos can import Roblox places (`.rbxl`, `.rbxm`, `.rbxlx` or `.rbxmx`)
and run their Luau scripts with the same `game`, `workspace`,
`Instance.new`, events, Players, Humanoids, Tools, RemoteEvents,
DataStores, TweenService, raycasts, welds and GUI. Simple places (obbies,
tycoon buttons, shops, round systems) work today; big games use things
Kronos doesn't have yet. See [what doesn't work yet](#what-doesnt-work-yet).

Recently added:

- **Compatibility Lab** that plays Roblox places headlessly and reports
  what breaks, plus Roblox **Tools** (hotbar on keys 1–9), wedge and
  cylinder mesh shapes, and much more GUI: text boxes, scrolling lists,
  grids, outlines, gradients, scaling and images.
- **Test multiplayer in Studio:** Test tab → **Server** starts a server and
  up to four Player windows on your PC. See [docs/MULTIPLAYER_TEST.md](docs/MULTIPLAYER_TEST.md).
- **Python game scripts (preview).** A game can include `Scripts/Main.py`
  next to its Luau scripts. Each Python script runs in its own sandbox
  with memory and time limits. See [docs/PYTHON_SCRIPTS.md](docs/PYTHON_SCRIPTS.md).
- **Rust plugins.** Write Studio plugins in Rust with the `kronos` crate.
  See [docs/PLUGIN_API.md](docs/PLUGIN_API.md).
- **Objects keep their identity** when you save and reopen a game, so
  welds and other links between parts are saved too.
- **Stricter chat filter** that catches spaced-out letters, symbols and
  look-alike letters, and blocks requests for personal details.
- **Windows fixes:** the window opens on more PCs, and Broken Bones now
  starts from the Player.

> Kronos is a **pre-beta**. Expect rough edges, and please
> [report anything that breaks](https://github.com/Bands-yt/kronos-platform/issues/new/choose).

![Playing as your avatar in Kronos Studio](docs/images/studio-play.png)

| The Player | Your avatar | Broken Bones |
|---|---|---|
| ![The Player's Home page](docs/images/player-home.png) | ![Customizing your avatar](docs/images/player-avatar.png) | ![Falling in Broken Bones](docs/images/broken-bones.png) |

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

## Bring a Roblox game

1. In Roblox Studio, save your place (File → Save to File). The usual
   `.rbxl` file is fine.
2. In Kronos Studio, open **File → Import Roblox file**, paste the file's
   path and press **Import**. The report shows how much of the place
   Kronos understands, as a score.
3. Press **Spawn Into Scene**, then **Play**. Scripts start like they do
   in Roblox: Scripts on the server side, LocalScripts for the player.

The score counts how many of the place's objects and script calls Kronos
understands. 100% doesn't promise the game plays the same, so Kronos also
has a **Compatibility Lab**: `kronos_compat --lab my_place.rbxlx` plays the
place headlessly for 30 seconds with physics and two test players, and
lists script errors (with time and line), deaths, parts falling out of the
world and missing images. See [docs/COMPAT_LAB.md](docs/COMPAT_LAB.md).
Only import places you made or have permission to use.

### How it's tested

Ten small test places (an obby, a door, a tycoon button, a shop, a
leaderboard, a simulator, a sword arena, an inventory, a round system and
a GUI menu) are in `engine/tests/compat_corpus`, each with a checks file
that plays it like a player would. The automated tests import all ten at
100%, and all ten pass their checks in the Lab (12 October 2026). One real place made in
Roblox Studio is also checked. That is a small sample: tell us about places
that break.

### What doesn't work yet

The full list per feature is in [docs/ROBLOX_BRIDGE.md](docs/ROBLOX_BRIDGE.md).
The big ones:

- **Nothing is downloaded from Roblox.** Meshes, images and sounds hosted
  by Roblox aren't fetched: `MeshPart`s with Roblox mesh ids are boxes, and
  GUI images must be shipped in the game's folder.
- **No animations.** `Humanoid:LoadAnimation` isn't built, and characters
  have only a `HumanoidRootPart` (no `Head` or limbs for scripts to find).
- **Multiplayer is early.** On a client, other players have no
  `Character`; servers send whole-world updates each tick, so big places
  are slow over the network.
- **Some GUI and 3D UI is missing:** one font for every `Font`, no rich
  text, `BillboardGui`, `SurfaceGui` or `ViewportFrame`. Clicking 3D parts
  (`ClickDetector`) only works from scripts and the Lab.
- **Data:** DataStores save to a file on the machine running the server,
  not to a shared online store yet; in Studio Play they're kept in memory.
- **Imports:** subtracted parts in unions aren't cut out; some newer
  `.rbxl` files (Zstandard-compressed) must be saved again as `.rbxlx`.

## Controls

| Input | What it does |
|---|---|
| WASD | Move |
| Space | Jump |
| Left Ctrl (hold) | Run |
| Right mouse (hold) and drag | Look around |
| Mouse wheel | Zoom |
| Shift | Shift lock: the character faces where you look |
| `/` or the chat button | Open chat (Player) |
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
| Script my game in Python (preview) | [docs/PYTHON_SCRIPTS.md](docs/PYTHON_SCRIPTS.md) |
| Import a Roblox game, or see which Roblox APIs work | [docs/ROBLOX_BRIDGE.md](docs/ROBLOX_BRIDGE.md) |
| Add music and sound | [docs/AUDIO_MIXER.md](docs/AUDIO_MIXER.md) |
| Learn how chat is filtered | [docs/CHAT_MODERATION.md](docs/CHAT_MODERATION.md) |
| Write a Studio plugin | [docs/PLUGIN_API.md](docs/PLUGIN_API.md) |
| Build a launcher or tool for Kronos | [docs/THIRD_PARTY_TOOLS.md](docs/THIRD_PARTY_TOOLS.md) |
| See what's coming next | [docs/ROADMAP.md](docs/ROADMAP.md) |
| Understand how the engine fits together | [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) |

## License

- **The plugin kit is open source** under the
  [Apache License 2.0](LICENSES/Apache-2.0.txt): the C plugin API, the
  example plugins, the Luau plugin template, the script type definitions
  and the plugin and script API docs. Build anything with them, including
  paid or closed-source tools. The full list is in
  [LICENSING.md](LICENSING.md).
- **Launchers, wrappers and other tools for Kronos are welcome.**
  [docs/THIRD_PARTY_TOOLS.md](docs/THIRD_PARTY_TOOLS.md) explains what's
  allowed and lists the launch options you can rely on.
- **Everything else is all rights reserved** (see [LICENSE](LICENSE)): the
  engine, Player, Studio and backend source is public to read, but copying,
  changing or reusing it needs written permission.

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
