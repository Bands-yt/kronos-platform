# Kronos Roadmap

What comes after `4.0.0-beta`, in the order we're building it. Every step
says when it counts as done, so progress is something you can check, not a
feeling. Estimates are rough, and the big ones are weeks, not hours.

| Release | Theme | Status |
|---|---|---|
| 4.1 | Creators: publishing, scripting, held-back fixes | Done locally, ships in the public beta |
| 4.2 | Scale: culling, batching, streaming, determinism, audio mixer | Done locally, ships in the public beta |
| Launch | Public beta (4.1 and 4.2 together), first Reddit post | Date to be decided by the owner |
| 4.3 | Roblox bridge: import a Roblox place and play it in Kronos | In progress: steps 1–5 done (score 82%) |
| 4.4 | Polish from launch feedback, plus the small 4.2 leftovers | Alongside 4.3 |
| 4.5 | Foundations: Rust plugins, Python scripts, permanent object IDs, engine clean-up | After 4.3 and 4.4 |
| 5.0 | Next-gen: graphics, web player, collaboration, media tools, 3D Model Maker | Parked until the bridge works |

## Already in the engine

These items from the AAA spec need hardening rather than building from scratch:

- **Data-oriented ECS.** Scenes are entt registries, so components are packed per type. There is no object-oriented scene graph to tear out.
- **Fixed-step accumulator.** `runtime/GameLoop` runs simulation and networking on fixed-rate accumulators decoupled from the render rate.
- **Plugin registry.** `core/PluginRegistry` and `NativePluginManager` load Studio and native plugins.
- **Script hot reload.** Saving a script reloads it on the next tick.
- **Rollback netcode foundation** (`net/RollbackSession`) and the **audio spectrogram**, both from 0.4.0.

---

## Launch — public beta (date to be decided)

Goal: someone on Reddit downloads Kronos on launch day, it opens on the first
try, and they make or play something within five minutes. No new features
until the post is up; this week is about making what exists work for a
stranger.

| Step | Done when | Who |
|---|---|---|
| Windows build passes CI with the MSVC runtime fix | `build.yml` is green on `4.1-creators` and the zip contains `vcruntime140.dll`, `vcruntime140_1.dll`, `msvcp140.dll` | Claude |
| Fresh-PC test | The Windows zip and installer start on a PC that never had Kronos or the VC++ redistributable, and the Linux download runs | Owner (Claude walks through it) |
| First-run check | A new project opens from the template, Play works, closing Studio leaves no crash report, Broken Bones starts | Claude — done (2026-10-06): Player → Create → Launch Studio → template → Save Game → plays from the Player; Play, clean exit, Broken Bones and the custom look in games all verified |
| Version bump and release `4.1.0-beta` | The three version numbers match, the tag is pushed, and the GitHub Release has every download | Claude, only after the owner says OK |
| Licence decision | `LICENSE` and the website say the same thing (today `LICENSE` says "All Rights Reserved"; don't call Kronos "open source" unless that changes) | Owner decides |
| Online or offline | Decide whether the post includes publishing and playing online. If yes: staging deploy with migration 011, publish → package → play verified, then production | Owner decides; deploys only with OK |
| Launch kit | A 30–60 s video (`marketing/pipeline`), 3–4 screenshots, `marketing/site` download links pointing at the release, a short README "what is this / how to start" | Claude makes, owner approves — README and 1 screenshot done; video, more screenshots and site links left |
| Feedback channel | GitHub issue templates for bugs and ideas, linked from the post and the site | Claude — forms done; link them from the post and site |
| Pick where to post | Each subreddit's rules read first (r/gaming is for players and strict about self-promotion; r/IndieDev, r/indiegames, r/playmygame and r/gamedev's showcase threads fit better) | Owner |

### Done locally (2026-10-06), waiting for release
- **My Games catalogue**: Studio's Save Game puts every project in the Player's Create page, with game pages (Play / Edit in Studio), play counts and "Jump back in" on Home. Broken Bones is listed and counts plays. See QUICKSTART "My Games".
- **Roblox controls**: hold right mouse to look, Shift for shift lock, wheel zoom, Ctrl to run, a camera that stays out of walls, smooth turning, and legs that turn toward strafes and walk backwards (fixes the moonwalk). Online play uses the same facing logic on the client.
- **Solid parts**: inserted parts get a matching anchored collider that follows resizing.
- **Blocky avatar**: a Roblox-style body: big rounded head with a smile, box torso, chunky arms and legs, T-shirt with short sleeves, hair cap. Shorter arms and neck (the shipped `.anim` files were converted to match). Works in the Avatar page, games, Studio and the Broken Bones ragdoll.

- **Play as your avatar in Studio**: Play spawns your avatar with the Player's controls and camera; Stop puts the scene and editor camera back. A part named `SpawnLocation` sets where players appear (Studio and Player). A long frame no longer stalls physics (Jolt is asked for at most 6 sub-steps).

- **Avatar page Customize tab** (Player): skin tone, Classic/Round head and five body sliders, saved to the profile and shown in every game (including Broken Bones, which runs as its own process and now reads the profile too); the preview frames the whole body; the Shop filters fit the card.
- **Games open in daylight, facing the build**: a game made in Studio starts at 2 pm with the day/night clock stopped, like Studio and Roblox (before, the Player's clock ran on its menus, so a game opened a few minutes after launch started at night, almost black). The player starts facing a `SpawnLocation`'s front, or the middle of the world when there is none (before, you started with your build behind you).
- **Feedback channel**: GitHub issue forms for bugs (`bug_report.yml`) and ideas (`idea.yml`).
- **README for newcomers**: what Kronos is, downloads, first game in five minutes, controls, where to report bugs, with `docs/images/studio-play.png`.

After the post: collect what people actually hit (crashes, confusing UI,
missing features) into the 4.4 list below before starting anything new.

---

## 4.3 — Roblox bridge

The idea: a Roblox creator should be able to bring a place over and see it
run. Kronos already has the importer's front half
(`engine/src/migration/`: `.rbxlx` parsing, property decoding, a hydrator
that spawns parts, lights and scripts into the scene, and a scanner that
lists unsupported Roblox APIs). What's missing is the part that makes the
imported scripts run: Roblox's object model and services in Luau.

Rules for this track:
- Build the most-used API first, measured by the compatibility score, not
  by guessing.
- Only test with places we made ourselves or that are clearly licensed for
  it. Never ship someone else's game, and don't use Roblox logos in
  marketing; describing the feature plainly ("imports `.rbxlx` places") is
  fine.
- Every step ends with tests, a visual check in Studio, and a doc section.
- Reflection first (third outside review, 7 October 2026): each class
  declares its properties, methods and events once in a class table, and
  `Instance.new`, the importer, the Studio inspector, saving and networking
  all read that table. A new creator-facing feature decides its Instance/API
  shape before it is built.

| # | Step | Done when | Size |
|---|---|---|---|
| 1 | **Compatibility score and test corpus** | A folder of small test places (obby, tycoon button, door, leaderboard, GUI shop) plus a headless `kronos_compat` tool that imports each one, runs it for N seconds and prints % of instances mapped, % of API calls supported, script errors and whether each place's expected behaviour happened (door opens, coin counted, and so on). Studio's import report shows the same score. | 1 week |
| 2 | **Datatypes** | `Vector3`, `CFrame`, `Color3`, `BrickColor`, `UDim`/`UDim2`, `Enum` (common items), `TweenInfo`, `NumberRange` work in Luau with Roblox's constructors, operators and common methods, checked against known values | 1–2 weeks |
| 3 | **Instance tree** | `game`, `workspace`, `script`, `Instance.new`, `.Name`, `.Parent`, `:FindFirstChild`, `:WaitForChild`, `:GetChildren`, `:GetDescendants`, `:Clone`, `:Destroy`, `:IsA`, attributes, built on the class table above. Part properties (`Position`, `Size`, `CFrame`, `Color`, `Anchored`, `CanCollide`, `Transparency`, `Material`) read and write the real ECS entity | 3–4 weeks |
| 4 | **Events** | `:Connect`/`:Disconnect`/`:Once`/`:Wait`, `.Touched`/`.TouchEnded`, `.Changed`, `:GetPropertyChangedSignal`, `RunService.Heartbeat`/`Stepped`/`RenderStepped` (plus `PreSimulation`/`PostSimulation` in a fixed, documented frame order), `wait`/`spawn`/`delay` mapped onto `task` | 1–2 weeks |
| 5 | **Players and characters** | `Players`, `LocalPlayer`, `PlayerAdded`/`PlayerRemoving`, `Character`/`CharacterAdded`, `Humanoid` (`WalkSpeed`, `JumpPower`, `Health`, `Died`, `MoveTo`) on the Kronos avatar; leaderstats show in the player list. The obby test place is playable start to finish | 3–4 weeks |
| 6 | **Client/server** | `RemoteEvent`/`RemoteFunction`, `ReplicatedStorage`, `ServerScriptService`, `StarterPlayerScripts`, LocalScripts running only on clients, on top of the existing `network` layer | 2–3 weeks |
| 7 | **DataStore** | `DataStoreService:GetDataStore`, `GetAsync`/`SetAsync`/`UpdateAsync`/`RemoveAsync`/`IncrementAsync` with Roblox-like limits. A local emulator file in Studio, and a backend table plus API for published games (backend deploy needs OK) | 2–3 weeks |
| 8 | **Common services** | `TweenService`, `Debris`, `SoundService`/`Sound` (on the 4.2 mixer), `Lighting` basics, `CollectionService` tags | 2 weeks |
| 9 | **GUI** | `ScreenGui`, `Frame`, `TextLabel`, `TextButton`, `ImageLabel`, `UIListLayout` subset, drawn by the existing UI layer with clicks wired up. The GUI shop test place works | 3–4 weeks |
| 10 | **Binary `.rbxl`/`.rbxm`** | Places saved in Roblox Studio's default binary format import the same as `.rbxlx` | 2 weeks |
| 11 | **Flagship demo** | A Roblox-made obby or tycoon imported, played with friends in Kronos, published to the Kronos catalog, and shown in a video | 1 week |

Done means: the test corpus scores at least 90%, and the flagship demo runs
start to finish without hand edits.

### Done
- **Step 1, compatibility score** (2026-10-07): `migration/CompatibilityScore`, the `kronos_compat` tool, five test places in `engine/tests/compat_corpus/`, and the score in Studio's Import window. Baseline: **34%** (instances 91%, API uses 3%, scripts 8%). The importer now reads Roblox Studio's CDATA script sources. Behaviour checks wait for scripts that run (steps 3–4). See `ROBLOX_BRIDGE.md`.
- **Step 2, datatypes** (2026-10-07): `Vector3`, `Vector2`, `CFrame`, `Color3`, `BrickColor`, `UDim`/`UDim2`, 27 `Enum`s, `TweenInfo`, `NumberRange`, `NumberSequence`, `ColorSequence`, `Ray`, `RaycastParams`, `Random` and a `typeof` that names them, in every script (`core/RobloxDatatypes.cpp`), checked by 58 tests against Roblox's results. Score 34% → 36%. See `ROBLOX_BRIDGE.md`.
- **Step 3, Instance tree** (2026-10-09): `game`, `workspace`, `script`, `Instance.new` and the common Instance methods (`FindFirstChild`, `WaitForChild`, `GetChildren`, `GetDescendants`, `Clone`, `Destroy`, `IsA`, attributes, ...) over the real ECS, built on a class table with about 60 classes whose properties carry saved/replicated/Studio flags (`core/InstanceTree.cpp`, `core/ScriptInstanceApi.cpp`). Part properties read and write the real entity. The importer keeps every known class and its stored properties, scenes save the Roblox data, and imported parts are now full size. Score 36% → 57%; scripts now stop at events (step 4). Checked in Studio's Debug Console and with an imported test place. See `ROBLOX_BRIDGE.md`.
- **Step 4, Events** (2026-10-09): Roblox signals with deferred handlers (`:Connect`, `:Once`, `:Wait`, `:Disconnect`), `Changed`/`GetPropertyChangedSignal`, attribute and tree events, `Destroying`, `Touched`/`TouchEnded` from real Jolt contacts, `BindableEvent`, and `RunService` (`Stepped`, `Heartbeat`, `RenderStepped`, `Pre`/`PostSimulation`, `PreRender`) in one documented frame order in both the Player and Studio Play. `task.delay`/`task.cancel` and the old `wait`/`spawn`/`delay`/`tick`/`time` globals. Parts made by scripts or imported are now solid (Anchored, CanCollide = false as a sensor, no body outside the workspace, parts inside Models in the right place). Score 57% → 75%; all four obby scripts run. Checked in Studio Play: the avatar stops at a script-made wall and `Touched` fires, walks through it with `CanCollide = false`. See `ROBLOX_BRIDGE.md`.
- **Step 5, Players and characters** (2026-10-09): `game.Players` with `LocalPlayer`, `PlayerAdded`/`PlayerRemoving` and `CharacterAdded`; your avatar is a character `Model` with a `HumanoidRootPart` and a `Humanoid` (`WalkSpeed`, `JumpPower`/`JumpHeight`, `Health`, `TakeDamage`, `Died`, `MoveTo`, `Running`/`Jumping`/`StateChanged`, ...) that drives the real Kronos controller (`core/RobloxPlayers.cpp`). Death at 0 health or below the fall height, respawn after 5 s at `SpawnLocation`. Players with `leaderstats` show in a top-right leaderboard in the Player and in Studio Play. Setting `CFrame`/`Position` now moves a simulating part's body, and imported `Script`s start on Play like in Roblox. Score 75% → 82%; the obby scores 100%. Checked in Studio Play with the imported obby (kill bricks, respawn, finish line) and tycoon (leaderboard, buy button). See `ROBLOX_BRIDGE.md`.

---

## 4.4 — Polish (alongside 4.3)

Small, high-value items, picked up between bridge steps:

- **Launch feedback**: the crashes and confusing spots people report after Friday, fixed first.
- **Per-source sound instances**: two entities using the same sound file can play at the same time (each `AudioSource` gets its own `ma_sound` via `ma_sound_init_copy`).
- **Keyframe-bound audio** (from 4.2): timeline events play sounds at exact sample times, through the shared mixer. 2–3 weeks.
- **Build cache** (from 4.2): a `KRONOS_COMPILER_CACHE` CMake option for sccache/ccache, and a content-hash cache for compiled shaders. 1 week. Distributed builds across machines only if build times become a real problem.
- **Third-party plugin sandbox on Windows** (Linux has it; Windows skips third-party plugins today).
- **Rollback-aware Luau**: scripts that run inside rollback matches.

---

## 4.5 — Foundations

Decided on 6 October 2026 after two outside reviews of the code; steps 7–9
added from a third review on 7 October. Some of
this makes the engine easier to grow; some opens it to more languages.

| # | Step | Done when | Size |
|---|---|---|---|
| 1 | **Permanent object IDs** | Every saved object has a GUID that survives renames, save/load and copy/paste, in both scene formats; old scenes get IDs when opened | 1 week |
| 2 | **Rust plugins** | A `kronos` Rust crate wraps `plugin/kronos_plugin.h` safely; a template Rust plugin builds with `cargo` and loads in Studio (in-process and sandboxed) | 1 week |
| 3 | **Python game scripts** | An embedded PocketPy runs `.py` scripts next to Luau with the same memory/time limits and security levels, and a first slice of the `world`/`events` API | 2–3 weeks |
| 4 | **Tighter script limits** | A per-game script memory budget plus a smaller per-script limit (today 256 MB per script), with a warning before the hard stop | 2–3 days |
| 5 | **Split the Renderer** | `Renderer.hpp`/`.cpp` (about 8,800 lines) split into context, frame, resource and pass pieces with no visible change, checked by tests and screenshots | 1–2 weeks |
| 6 | **Server program without graphics** | A separate `kronos_server` that doesn't link SDL or Vulkan and runs published games | 1–2 weeks |
| 7 | **Games out of the engine library** | Broken Bones, TNT Wars, Mining Sim, Despair and the trailer code move out of `engine_core` into their own targets; the network protocol carries generic messages instead of game ones (`FireWeapon`, `SelectClass`, ...) | 1–2 weeks |
| 8 | **One version number** | `engine/CMakeLists.txt` `project(... VERSION ...)` is the single source; `KronosVersion.hpp` and the installer are generated from it | 1 day |
| 9 | **Moderation data on the backend** | Reports and moderation logs go to the backend database, not files next to the game server | 3–5 days |

Later in this track, once the above is done: a real render graph (passes
declare what they read and write), a command buffer between scripts and the
simulation, and Studio service interfaces for plugins.

---

## 4.1 — Creators

Ships what is finished but private today, plus the creator-facing gaps around it.

### Done locally, waiting for release

- **Broken Bones:**
  - the sun sits on the lagoon side with lighter shadows;
  - every map has random rim terrain ringing the lake;
  - the volcano horizon band is fixed;
  - rebirth tiers unlock at level 20, then 40, 60 and so on, with six rebirth-only upgrades.
- **Anyone's game is playable by everyone.** Studio's Publish uploads the whole game folder as a package. Servers and joining clients download and run it by slug, and the catalog lists it automatically, with no manual `import-games.js` sync. Archives are path-traversal safe and size-capped.
- **Script editor revamp:**
  - Luau autocomplete with kind badges, type details and API docs;
  - signature help that highlights the active parameter;
  - snippets;
  - live type errors and lint warnings with squiggles, inline messages and a Problems panel;
  - find/replace;
  - comment toggle, duplicate line and go-to-symbol;
  - zoom and three themes;
  - JetBrains Mono, a save-state header and starter templates.
- **Catalog releases:**
  - every upload is a numbered version, and creators roll back to any approved one instantly from Studio's Releases section;
  - new games and new versions wait in a moderator review queue (`GAME_REVIEW_REQUIRED`), and players keep the previous version meanwhile;
  - per-account storage quota, counting identical bytes once;
  - reviewed thumbnails and screenshots, uploaded from Studio, with thumbnails captured as PNG;
  - server-side manifest validation: the backend opens the archive and rejects anything a server could not run.
- **Dedicated servers host the downloaded game** headlessly (scene loads without a GPU) and shut down cleanly on SIGTERM.
- **Logins are kept per backend:** pointing a build at staging or a local server no longer overwrites or deletes the production sign-in.
- **New app icons** (direction A, "Orbit K with badge") for Player, Studio, 3D Tools, Movie Maker and Audio: multi-size Windows `.ico`, Linux hicolor PNGs 16–512 and scalable SVG.
- **Script editor, second round:** go to definition, rename symbol, find in all scripts, multi-cursor, and a breakpoint/step/pause debugger wired to the Luau VM.
- **Shader graph compiles to SPIR-V:** 34 node kinds (inputs, constants, math, noise/checker/fresnel patterns, vector split/combine). The graph is compiled into the real forward shader for the renderer's active variant (RT/bindless), so graph materials keep lighting and shadows. Objects get a graph material that is saved in both scene formats and covered by undo; the editor live-updates the objects it is editing, adds nodes from a right-click menu, and saves and opens `.kshader` files.
- **Visual scripting:** 65 node kinds (events, flow, actions, objects, variables, values, math, vectors, logic). Graphs compile to Luau and then to Luau bytecode, so they run anywhere a hand-written script runs, and the editor points at the node behind any compile error. Yielding nodes like Wait work inside events. Objects keep their graph in both scene formats, the editor live-updates running scripts in Play, and it saves and opens `.kvs` files. Studio Play now exposes the `world` API and collision events to scripts, and Stop restores the scene.
- **Resource layer** (`core/ResourceManager`): reference-counted, generation-checked handles; files decode on worker threads and upload on the main thread under a per-frame budget; a handle is usable the moment it is acquired, and the data appears when the upload lands. The same file is loaded once however many objects use it, and unloads a few frames after the last user goes away. `.kbundle` manifests group resources, can require other bundles, declare dependencies between files, and report progress; bundles share what they have in common, and cycles are refused. Hot reload swaps meshes, textures and audio in place behind the same handle (a broken save keeps the last good version), and notifies everything that depends on the changed file. Studio scenes and model imports load through it, with a Resources panel; the player uses it for scene meshes (hot reload with `KRONOS_HOT_RELOAD=1`).
- **Zero-copy hot reload for native plugins:** a plugin declares its state once with `KRONOS_HOT_RELOAD_STATE(Type, version)` (`core/HotReloadState.hpp`). The engine owns that memory, so when a rebuilt plugin has the same state layout its new code is handed the very same object: nothing is serialized or copied, and vectors, maps and counters carry straight over. When the layout changes, an optional `kronosHotReloadMigrateState` carries values into the new layout; without one the state starts fresh. Studio watches plugin libraries and swaps a rebuild in once the file stops changing, a broken build keeps the previous one running, and the Resources panel lists each plugin's state, reload count and last error (the player does the same with `KRONOS_HOT_RELOAD=1`).

### Still to build

| Item | Estimate |
|---|---|
| Deploy the backend and verify publish → package → play end to end on staging (verified locally; needs migration 011 and an OK to deploy) | 2–3 days |

---

## 4.2 — Scale

Engine internals so large worlds run well. This is mostly the ROLE & MANDATE spec.

### Done locally

- **BVH for culling and raycasts** (`core/Bvh`, `core/SceneSpatialIndex`): a dynamic AABB tree (surface-area insertion, rotations for balance, fat leaves so small moves cost nothing). The scene index syncs incrementally from the ECS each frame (an unchanged object costs a few compares; parented objects follow their parents) and keeps the world matrices it computes for the renderer. The main pass and glass pass now draw only what touches the camera frustum, instanced batches skip culled instances, Studio's Stats panel shows drawn/culled counts, and viewport picking walks the tree instead of testing every object. Before this there was no frustum culling at all.
- **Static batching** (`core/StaticBatching`): automatic, no "mark as static" step. An object that hasn't moved or changed its material for about a second joins a batch with the other still objects that share its material in the same 32 m cell, merged into one world-space mesh and drawn (and shadow-cast) in one call. Moving, editing or deleting an object pulls it out on the next frame and the batch is rebuilt without it; each move makes it wait four times longer to rejoin, and after three it stays unbatched. Batches are frustum culled, mirrored objects keep their winding, rebuilds and mesh downloads are budgeted per frame, and replaced batch meshes are freed only after the frames in flight are done with them. Stats shows the batch count. On the 400-box test scene, 400 boxes in four colours become 48 batches.
- **Automatic instancing**: objects that aren't static-batched and share a mesh with at least three others are drawn with one instanced call, each keeping its own colour, material values and (with bindless) textures. Shadow casters are instanced the same way per cascade and spot light, with a new `shadow_instanced.vert`. Scene loading now gives identical primitives one shared mesh (Modeling Mode already copies a mesh before editing it), so this applies to ordinary scenes. It also fixes `Renderable::instanced` objects with a parent being drawn at their local position. On the 400-box test scene with unique colours, draw calls went from 889 to 206.
- **Vulkan 1.4 feature tier** (`core/GpuFeatures`): the renderer detects variable rate shading, host image copy, compute shader derivatives and shader clocks, enables whichever the GPU has, and reports them in Lighting Tools. Each one is optional, and a GPU without it renders exactly as before. Host image copy: new textures go straight from memory into the GPU image with no staging buffer or queue wait, used only where the driver says sampling stays just as fast. Variable rate shading: a "Coarse Shading" toggle (always on in Performance Mode) shades the opaque scene once per 2x2 pixels. Shader clock: a "Shader Cost View" colours each pixel by the GPU cycles its shading took. Compute derivatives are enabled for future compute passes; no shipped shader uses them yet. The app still targets Vulkan 1.3 and uses these as extensions, so 1.3 drivers that have them benefit too.
- **Spatial world streaming** (`core/WorldStreaming`): a scene can be split into square cells kept next to it in `<scene>.world/` (a `world.kworld` manifest plus one ordinary scene file per cell). Cells are a new resource kind, so they decode on the resource layer's workers and appear without a hitch; a cell loads when the camera or any `StreamingSource` (players get one automatically) comes within the load radius and unloads once every source is past the unload radius. Unloading destroys the cell's entities, physics bodies and scripts, and frees meshes no other cell uses once the frames in flight are done with them. Studio has a World Streaming panel: split a saved scene into cells (scripted, moving and very large objects stay in the main scene), move new objects in later, tune the radii, load every cell, and watch a live map of loaded cells around the camera. In Studio, an edited cell that streams out keeps its changes in memory, and saving the scene writes changed cells (objects moved across cells go with them when both cells are loaded). Save As copies the world. Publishing flattens every cell into the package, so published packages don't stream yet; project-based games do, and a dedicated server streams around entities with a `StreamingSource` or, with none, keeps every cell loaded. On a 1,600-object test world, 36 of 400 cells load around the camera: 157 objects and 86 draw calls instead of 1,601 and 441.
- **Deterministic physics and rollback matches** (`core/PhysicsRollback`, `net/RollbackProtocol`, `net/RollbackNetSession`, see `docs/ROLLBACK.md`): Jolt is now built cross-platform deterministic and engine code no longer fuses multiply-adds, so the same inputs give bit-identical physics whatever the core count, and should across machines too (Jolt's cross-platform guarantee; only tested on one machine so far). Physics state can be snapshotted, restored and hashed, and contact events come out in a fixed order. `PhysicsRollback` runs GGPO-style rollback over the whole physics world: predicted remote inputs, snapshot restore and resimulation when a real input differs, and checksums of confirmed frames so a desync is caught within a few frames. Inputs travel in compact unreliable packets that repeat anything not yet acknowledged, and faster peers slow down slightly to stay level with slower ones. `engine_runtime --rollback-host N` / `--rollback-join <address>` play a peer-to-peer match: the world stays frozen until everyone joins, a joiner whose scene differs from the host's is stopped, then every player gets a physics character driven by a deterministic copy of the normal movement code (moving platforms are driven inside the simulation too). Matches can be recorded as replays, and replay tests check resimulation stays bit-identical with different thread counts. Tested with two peers over 20% packet loss and jitter (bit-identical to a no-latency reference), over real ENet with 10% loss, and with two Kronos Player windows on one machine. Rollback-aware Luau, where scripts run inside the simulation, is still to do: today scripts in a match must leave physics alone.
- **Plugin API with sandboxing** (`plugin/kronos_plugin.h`, `plugin/PluginHost`, see `docs/PLUGIN_API.md`): a stable C interface, so plugins can be written in any language and keep working across engine updates until the API's major version changes; structs carry their size, so new fields can be added without breaking old plugins. Plugins declare capabilities (read or change the scene, import assets, add editor panels, share memory) and every call is checked. Asset importers and editor panels live in versioned registries: the highest version wins, and removing it brings the older one back. Named shared-memory channels survive plugin reloads and crashes. Plugins in `native_plugins` run in-process and hot reload; third-party plugins (in the user's plugin folder) always run in their own `kronos_plugin_sandbox` process on Linux, locked down with Landlock (read only the plugin's folder and system libraries, write nothing), seccomp (no network, exec, fork, ptrace or signalling other processes) and resource limits, and talk to the editor over a socket. A plugin that crashes or hangs is stopped without taking Studio down, and the Resources panel shows why and can restart it. Dropping a file a plugin can import converts it and imports the result. Includes a sample plugin (Grid Snap panel and a PGM heightmap → OBJ terrain importer). Not done yet: the sandbox on Windows and macOS (third-party plugins aren't loaded there), and a way in Studio to grant a plugin fewer capabilities than it asks for.
- **Audio mixer** (`core/AudioMixer`, `core/ScriptAudioApi`, see `docs/AUDIO_MIXER.md`): every sound plays on a bus (Master with Music, SFX, Voice and UI by default). Buses nest, and each has volume, mute, solo, low-pass and high-pass filters, reverb, pre/post sends and ducking (Music ducks under Voice). Snapshots such as "Paused" and "Underwater" fade in and out at any intensity, blending volumes and reverb linearly and filters by pitch. Studio has a Mixer panel (strips with live meters, a bus editor, a snapshot editor and test sounds), and the Inspector has a Sound section (file, volume, pitch, loop, play on start, 3D distance and bus, with preview and undo). Scenes save their sounds, Studio Play plays them through the mixer and restores it on Stop, and the player loads the project's `mixer.kmixer`. Scripts control it all through the `audio` table. `KRONOS_SILENT_AUDIO=1` runs the audio engine without sending anything to the speakers, for automated tests.

### Still to build

Keyframe-bound audio and the build cache moved to 4.4 above.

---

## 5.0 — Next-gen (parked)

These stay on the list but wait until the Roblox bridge works, because a
creator moving their game over matters more right now than new rendering
tech. Several also need vendor SDKs or licence decisions first.

Large, independent tracks. Each could be its own release, and several need vendor SDKs or licensing decisions first.

| Item | Estimate |
|---|---|
| Ray-traced global illumination (shadows already exist) | 6–10 weeks |
| Upscaling: FSR 3 first (open source), then DLSS and XeSS (vendor SDKs and licences) | 3–6 weeks each |
| V8 (JavaScript) and Pybind11 (Python) scripting on the existing polyglot layer | 3–6 weeks |
| Real-time collaboration in Studio (CRDT scene sync) | 2–3 months |
| WASM/WebGPU build of the player | 2–3 months |
| Wave-based 3D audio with HRTF | 2–4 months |
| VST3 plugin hosting in Kronos Audio (licensing review first) | 4–6 weeks |
| GPU spectral editing on top of the spectrogram | 4–6 weeks |
| Movie Maker: proxies, compositing, planar tracking | 3–6 months |
| 3D Tools: GPU modifiers, dynamic-topology sculpting, USD import/export | 2–4 months together |
| Gaussian splat viewer (NeRF much later, if at all) | 1–2 months |
| ONNX Runtime / TensorRT inference for AI tooling | 3–6 weeks |
| GPU destruction, unified physics, motion matching | 1–3 months each |
| 3D Model Maker in Studio: primitives, vertex/edge/face editing, materials and UVs, import/export (see `MODEL_MAKER_AI_ROADMAP.md`) | 2–3 months |
| AI model generator: first "Import AI Model" from outside tools, later an optional online generator (costs money to run) | 3–6 weeks for the first part |

## Out of scope for now

- Rewriting the ECS or the game loop. Both already follow the data-oriented, fixed-step design the spec asks for, so the work goes into profiling and hardening instead.
- A home-grown distributed build scheduler. Existing tools cover it.
