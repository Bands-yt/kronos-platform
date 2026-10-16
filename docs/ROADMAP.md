# Kronos Roadmap

The current release is `4.4.1-pre-beta`, "v4 pre-beta (update 1)" (4.1, 4.2, the 4.3 Roblox bridge, 4.4 polish and the first 4.5 steps). This is what we're building, in order. Every step
says when it counts as done, so progress is something you can check, not a
feeling. Estimates are rough, and the big ones are weeks, not hours.

| Release | Theme | Status |
|---|---|---|
| 4.1 | Creators: publishing, scripting, held-back fixes | Released in 4.4.0-pre-beta |
| 4.2 | Scale: culling, batching, streaming, determinism, audio mixer | Released in 4.4.0-pre-beta |
| Launch | Public beta (4.1 and 4.2 together), first Reddit post | Date to be decided by the owner |
| 4.3 | Roblox bridge: import a Roblox place and play it in Kronos | Released in 4.4.0-pre-beta: steps 1–10 and replication (score 100% on the test places); step 11 (flagship demo) waits for a Roblox-made place |
| 4.4 | Polish from launch feedback, plus the small 4.2 leftovers | Mostly done and released in 4.4.0-pre-beta; the Windows plugin sandbox is left |
| 4.5 | Foundations: Rust plugins, Python scripts, permanent object IDs, engine clean-up | In progress; steps 1, 2, 4, 8 and a first Python slice released in 4.4.0-pre-beta |
| 4.6 | Prove it works: Compatibility Lab, complete GUI and images, honest docs (the advisor's Milestone A); Studio first round (Milestone B) | Steps 1–6 and 8 done; step 7 (server program) is unblocked, not started |
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
| Windows build passes CI with the MSVC runtime fix | `build.yml` is green on `4.1-creators` and the zip contains `vcruntime140.dll`, `vcruntime140_1.dll`, `msvcp140.dll` | Dev |
| Fresh-PC test | The Windows zip and installer start on a PC that never had Kronos or the VC++ redistributable, and the Linux download runs | Owner |
| First-run check | A new project opens from the template, Play works, closing Studio leaves no crash report, Broken Bones starts | Dev — done (2026-10-06): Player → Create → Launch Studio → template → Save Game → plays from the Player; Play, clean exit, Broken Bones and the custom look in games all verified |
| Version bump and release `4.1.0-beta` | The three version numbers match, the tag is pushed, and the GitHub Release has every download | Dev, only after the owner says OK |
| Licence decision | `LICENSE` and the website say the same thing (today `LICENSE` says "All Rights Reserved"; don't call Kronos "open source" unless that changes) | Owner decides |
| Online or offline | Decide whether the post includes publishing and playing online. If yes: staging deploy with migration 011, publish → package → play verified, then production | Owner decides; deploys only with OK |
| Launch kit | A 30–60 s video (`marketing/pipeline`), 3–4 screenshots, `marketing/site` download links pointing at the release, a short README "what is this / how to start" | Dev makes, owner approves — README and 1 screenshot done; video, more screenshots and site links left |
| Feedback channel | GitHub issue templates for bugs and ideas, linked from the post and the site | Dev — forms done; link them from the post and site |
| Pick where to post | Each subreddit's rules read first (r/gaming is for players and strict about self-promotion; r/IndieDev, r/indiegames, r/playmygame and r/gamedev's showcase threads fit better) | Owner |

### Done locally (2026-10-06), waiting for release
- **My Games catalogue**: Studio's Save Game puts every project in the Player's Create page, with game pages (Play / Edit in Studio), play counts and "Jump back in" on Home. Broken Bones is listed and counts plays. See QUICKSTART "My Games".
- **Roblox controls**: hold right mouse to look, Shift for shift lock, wheel zoom, Ctrl to run, a camera that stays out of walls, smooth turning, and legs that turn toward strafes and walk backwards (fixes the moonwalk). Online play uses the same facing logic on the client.
- **Solid parts**: inserted parts get a matching anchored collider that follows resizing.
- **Blocky avatar**: a Roblox-style body: big rounded head with a smile, box torso, chunky arms and legs, T-shirt with short sleeves, hair cap. Shorter arms and neck (the shipped `.anim` files were converted to match). Works in the Avatar page, games, Studio and the Broken Bones ragdoll.

- **Play as your avatar in Studio**: Play spawns your avatar with the Player's controls and camera; Stop puts the scene and editor camera back. A part named `SpawnLocation` sets where players appear (Studio and Player). A long frame no longer stalls physics (Jolt is asked for at most 6 sub-steps).

- **Avatar page Customize tab** (Player): skin tone, Classic/Round head and five body sliders, saved to the profile and shown in every game (including Broken Bones, which runs as its own process and now reads the profile too); the preview frames the whole body; the Shop filters fit the card.
- **Games open in daylight, facing the build**: a game made in Studio starts at 2 pm with the day/night clock stopped, like Studio and Roblox (before, the Player's clock ran on its menus, so a game opened a few minutes after launch started at night, almost black). The player starts facing a `SpawnLocation`'s front, or the middle of the world when there is none (before, you started with your build behind you).
- **In-game chat works** (2026-10-09): `/` or the new chat button (top-left) opens it. A quick tap of a key is no longer missed (the Player used to miss taps shorter than a game tick). Playing alone now runs messages through the same moderation as a server (rate limit, profanity filter, local classifier, Gemini when `GEMINI_API_KEY` is set) and shows refusals in the chat box. `KRONOS_CHAT_REVIEW_LOG` saves every message with its local and Gemini verdicts as JSON lines, for training a moderation model. See `CHAT_MODERATION.md`.
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
- **Step 6, Client and server, part 1** (2026-10-09): Roblox `Script`s run on a server side and `LocalScript`s on a client side, each side one shared Luau VM (`_G`, `shared`), with Roblox's rules for where scripts run and what stops them (`core/RobloxScripts.cpp`). Starter folders are copied into the player, character, `Backpack` and `PlayerGui`. `require(ModuleScript)` with Roblox's caching and errors. `RemoteEvent`/`UnreliableRemoteEvent`/`RemoteFunction`/`BindableFunction` work between the two sides in one program, and the client can't see `ServerStorage`/`ServerScriptService`. Score 82% → 85%. Checked in Studio Play with an imported test place (client asks the server, server answers and builds a part, server messages the client). Also fixed: Stop left an empty "Entity N" in an imported Workspace. See `ROBLOX_BRIDGE.md`.
- **Step 6, part 2: over the network** (2026-10-09): remotes and the player list now work between a server and players on other computers (`core/RobloxRemoteNet.cpp`, network protocol version 2). Arguments keep their types (tables, Vector3, CFrame, Players, other Instances by path). Limits like Roblox's: 64 KiB per message, 900 bytes per `UnreliableRemoteEvent`, and 240 calls at once then 120 per second per player. Fixed on the way: dedicated servers (`--headless`) never started game scripts, and a player joining with `--game` lost their body when the game loaded. Checked with a real server and player, with and without a window; `engine/tests/remote_network_smoke.sh` repeats the check.
- **Replication** (2026-10-09): the server copies its objects to every player, like Roblox (`core/RobloxReplication.cpp`). It sends a full copy on join, then only changes, 20 times a second, and pending changes always go out before a remote. `ServerStorage` and `ServerScriptService` stay on the server. The first copy matches what the client already loaded instead of duplicating it, and LocalScripts wait for it. Also new: parts made, moved or changed while a game runs get the right physics body, and destroyed parts lose theirs (`core/PartBodies.cpp`; also fixes Studio Play). Checked with tests, the two-program smoke test (the client sees the server's new part) and on screen in the Player and Studio.
- **Step 7, DataStore** (2026-10-10): `DataStoreService` with `GetDataStore`/`GetGlobalDataStore` and `GetAsync`, `SetAsync`, `UpdateAsync`, `RemoveAsync`, `IncrementAsync` (`core/RobloxDataStore.cpp`). Roblox's value rules and error messages, 50-character names and keys, 4 MB values, and the request budget (over it only warns). Clients get Roblox's "can't be accessed from client" error. Data is saved per game in a local `datastores/<game>.json` file; Studio Play keeps it in memory. Score 85% → 89% (the leaderboard place scores 100%). Checked with a real server in `remote_network_smoke.sh` (loads a saved 41, saves 42). Still to do: the backend table and API, so data follows players between servers (needs a deploy OK).
- **Step 8, Common services** (2026-10-10): `TweenService` (all easing styles, repeats, reverses, delay, Pause/Cancel, `Completed`), `Debris:AddItem`, `CollectionService` tags (saved, cloned, replicated, imported from `.rbxlx`, added/removed signals), `Sound` on the Kronos mixer (Play/Stop/Pause/Resume, `Ended`, 3D when inside a part), and `Lighting` (`ClockTime`/`TimeOfDay`, Brightness, ambient, fog) in the Player (`core/RobloxServices.cpp`). Score 89% → 96%; the door and tycoon places now score 100%. Checked in Studio Play: a tween moves and recolours a part and reports `Completed`. Roblox sound ids can't be downloaded; use files from the game folder.
- **Step 9, GUI** (2026-10-10): `ScreenGui`, `Frame`, `TextLabel`, `TextButton`, `ImageLabel`/`ImageButton` (background only), `UIListLayout`, `UICorner`, `UIPadding`, with `UDim`/`UDim2`/`Vector2` properties (`core/RobloxGui.cpp`). Layout with scale + offset, `AnchorPoint`, `ZIndex`, `DisplayOrder` and clipping; clicks (`MouseButton1Click`, `Activated`, `MouseButton1Down/Up`) and hover (`MouseEnter/Leave`); GUI properties import from `.rbxlx`. Drawn in the Player and during Studio Play. Score 96% → 100% on all five test places. Checked in Studio Play: a script-made shop panel draws with rounded corners and a list layout, and clicking its button runs the script.
- **Step 10, binary `.rbxl`/`.rbxm`** (2026-10-10): Roblox Studio's default binary files import the same as `.rbxlx` (`migration/RbxBinaryReader.cpp`: LZ4 chunks, all common property types including compact CFrame rotations). Studio's File → Import Roblox file and `kronos_compat` take both. Tested against 13 binary/XML pairs from rbx-dom's MIT test set (every shared property matches). Checked in Studio: a binary `.rbxm` imports three named, coloured parts, and Undo removes them. Not yet: Zstandard-compressed files, Font/SharedString/sequence properties.

---

## 4.4 — Polish (alongside 4.3)

Small, high-value items, picked up between bridge steps:

- **Launch feedback**: the crashes and confusing spots people report after Friday, fixed first.
- **Roblox basics from the advisor**: `workspace:Raycast` ✔, `WeldConstraint` ✔, `workspace.CurrentCamera` ✔, `GetPivot`/`PivotTo` ✔, `Weld`/`Motor6D` ✔, more server security ✔.
- **Studio feel**: the owner found Roblox Studio nicer to use. In small, tested steps: audit ✔, Roblox-style Explorer tree ✔, Roblox Properties in the Inspector ✔ (STUDIO_FEEL.md); Explorer search ✔, Insert Object ✔, picking object references ✔, Script Editor end-of-file line ✔; undo for Insert Object ✔, Roblox's property categories ✔.
- **Import fixes from the first Roblox-made place**: warn before spawning a second copy ✔; an imported Baseplate replaces the template Ground ✔; unions keep their shape ✔; ragdoll on death ✔.
- **Keyframe-bound audio** (from 4.2): timeline events play sounds at exact sample times, through the shared mixer ✔.
- **Build cache** (from 4.2): a `KRONOS_COMPILER_CACHE` CMake option for sccache/ccache ✔, and a content-hash cache for compiled shaders ✔. Distributed builds across machines only if build times become a real problem.
- **Third-party plugin sandbox on Windows** (Linux has it; Windows skips third-party plugins today).
- **Rollback-aware Luau**: scripts that run inside rollback matches ✔.

### Done
- **Stricter chat filter** (11 Oct 2026): swear words typed on the test laptop got through. The profanity filter was rewritten to catch leetspeak, look-alike letters, hidden characters, stretched letters, endings and words spaced out letter by letter, without censoring normal words like "class" or "hello". The local classifier also blocks more threats and requests for personal details (school, address, Snapchat). See [CHAT_MODERATION.md](CHAT_MODERATION.md).
- **One Player at a time** (11 Oct 2026): opening Kronos again no longer starts a second home screen; on Windows the open one comes to the front. Games, servers and `kronos://` links are not affected. `KRONOS_ALLOW_MULTIPLE=1` turns it off for testing.
- **Broken Bones on Windows** (11 Oct 2026): tested on a real Windows laptop, the Player counted a play but never opened Broken Bones, because it relaunched itself as `engine_runtime` without `.exe`. Programs next to the Player are now found with the right name on each system (`core::siblingExecutable`), a play only counts once the game really starts, and a test checks the path on Windows CI. The same laptop confirmed the 4.3.1 Vulkan fix: the window opens and Player and Studio run.
- **Rollback-aware Luau** (11 Oct 2026): a game's `Scripts/Match.luau` runs inside rollback matches, once per simulated frame, and is rewound with the physics. All match data lives in one `state` table that's saved with every snapshot (encoded with sorted keys, so equal states give equal bytes and checksums); everything else is frozen after loading, and changing a top-level local from a function is refused at load. `math.random` and the trig functions are deterministic, an instruction budget stops endless loops the same way everywhere, and `ctx` gives inputs, player bodies and physics calls. Tested with two peers over a lossy link (same state as a run without rollbacks) and a bit-identical replay. See ROLLBACK "Match scripts".
- **Keyframe-bound audio** (11 Oct 2026): Movie Mode's Audio-track events now play. Each one is scheduled about 0.1 s ahead on the audio clock, so it starts on the exact sample of its keyframe, not on the next game frame; loops repeat it, and pausing, scrubbing or jumping cancels cues that haven't started. An event's text names the sound file (extension optional), looked up next to the project, in its `audio/` folder and in Kronos's assets. Sounds go through the SFX bus. Tested by mixing offline and finding the exact start sample. See AUDIO_MIXER "Movie Mode audio".
- **Build cache** (11 Oct 2026): `-DKRONOS_COMPILER_CACHE=auto` (or `ccache`/`sccache`) puts a compiler cache in front of every C++ compile, and `-DKRONOS_SHADER_CACHE_DIR=<folder>` keeps compiled shaders keyed by their content, the shared shader library, the flags and the compiler version (`engine/cmake/CachedGlslc.cmake`). Rebuilding all 39 shaders from the cache took 0.7 s instead of 4.8 s. Both are off by default. See QUICKSTART "Build options".
- **Player polish, batch 1** (10 Oct 2026): the top bar's search box shrinks so it never covers the bell or profile in a small window; the profile icon shows your skin tone; the Avatar Shop's empty message points to the Customize tab with an "Open Customize" button; the spinning-logo panel hides when the window is narrower than 1180 px; games now use Studio's daytime light (clear blue sky, fixed exposure, no volumetric haze), so a game looks the same in the Player as in Studio Play.
- **Per-source sound instances** (10 Oct 2026): each object with a sound gets its own voice (`ma_sound_init_copy`, or a buffer reference for generated sounds), so objects sharing a sound file play at the same time instead of cutting each other off. Voices are freed when the object is destroyed, rebuilt when the sound is reloaded, and released on Studio's Stop. Tested by mixing offline and measuring the output.
- **`workspace:Raycast`** (10 Oct 2026): rays through the real physics world with `RaycastParams` (Exclude/Include filters with descendants, `RespectCanCollide`); results have Instance, Position, Normal, Distance and Material. See ROBLOX_BRIDGE "Raycasts".
- **`WeldConstraint`** (10 Oct 2026): welded parts move as one (a Jolt fixed joint per weld); setting `CFrame` moves the whole assembly, `Position` moves one part and keeps the new offset; imported places keep their `Part0`/`Part1` links (all Instance-valued properties are now linked on import). See ROBLOX_BRIDGE "Welds".
- **`workspace.CurrentCamera`** (11 Oct 2026): scripts read and move the player's camera (`CFrame`, `FieldOfView`, `CameraType` Scriptable/Custom) and convert between screen and world (`ViewportPointToRay`, `WorldToViewportPoint`). See ROBLOX_BRIDGE "Camera".
- **Bridge Doctor** (11 Oct 2026): `kronos_compat --report-md/--report-json` explains what a Roblox place needs: findings sorted into blockers, gameplay risks, visual/audio and information, with script lines when Kronos has them, and byte-stable Markdown/JSON reports. Places can carry behaviour checks (`<place>.checks.luau`); the five test places now have 16, all passing. First real Roblox-made place (the owner's obby, built in Roblox Studio): imports, all 3 scripts run, 5/5 behaviour checks pass (kill brick kills, finish pad reacts). See BRIDGE_DOCTOR.md.
- **Studio feel, step 1** (11 Oct 2026): the Explorer shows Workspace and the services like Roblox Studio (class icons, empty services hidden; the old grouped view is one click away), and the Inspector has a Properties section with every Roblox property of the selected object (filter, dropdowns for enums, undo). Then: right-click Insert Object, Explorer search, object references picked from a list, and Script Editor errors at the end of a script on the right line. See STUDIO_FEEL.md.
- **`Weld`, `ManualWeld`, `Motor6D`** (11 Oct 2026): joints with `C0`/`C1` place Part1 like Roblox and hold it with a physics joint; `Motor6D.Transform` turns it. See ROBLOX_BRIDGE "Weld, ManualWeld and Motor6D".
- **Import fixes** (11 Oct 2026): Studio asks before spawning a place that's already in the scene, and a place with its own Baseplate replaces the template Ground (undo brings it back). Windows test run: LAN discovery tests are Linux-only like the feature, and a test no longer crashes when nothing is discovered.
- **GetPivot / PivotTo** (11 Oct 2026): parts and models can be moved and turned by their pivot, plus `Get/SetPrimaryPartCFrame`; the first Roblox-made place used `GetPivot`. See ROBLOX_BRIDGE.md "Pivots".
- **Unions** (11 Oct 2026): a Roblox union is drawn and collided from the parts it was made from (read from the file), so the owner's obby staircase keeps its steps instead of being one block. Scripts still see one part. Roblox `SharedString` values are now read from both file types. See ROBLOX_BRIDGE "Unions".
- **Ragdoll on death** (11 Oct 2026): when a Humanoid's Health reaches 0 the player's avatar falls as a physics ragdoll (in the Player and in Studio Play) and the camera follows it; the respawn removes it. Ragdoll bodies no longer pretend to be entity 0 in touch and collision events.
- **Studio feel, step 2** (11 Oct 2026): Insert Object can be undone and redone, and the Properties section uses Roblox Studio's categories (Appearance, Data, Text, Transform, Behavior, ...). See STUDIO_FEEL.md.
- **Server security** (11 Oct 2026): objects a client names in a remote call must be ones it can see (server-only folders and other players' GUI arrive as nil, and remotes kept in `ServerStorage` can't be fired), and parts can't be moved or sized to NaN/infinity by scripts or anything else, so a cheating client can't break the physics world.
- **Validate Project** (11 Oct 2026): File > Validate Project... in Studio checks the open scene: scripts compile, Roblox APIs exist, object references and weld parts are set, asset files exist, script errors from the last Play, and what can't be saved yet. Each check says PASS/FAIL/WARNING/NOT RUN/UNAVAILABLE; findings are clickable (select the object, open the script at the line), filterable, compared with the previous run, and exportable as Markdown/JSON. See VALIDATE_PROJECT.md.

---

## 4.5 — Foundations

Decided on 6 October 2026 after two outside reviews of the code; steps 7–9
added from a third review on 7 October. Some of
this makes the engine easier to grow; some opens it to more languages.

| # | Step | Done when | Size |
|---|---|---|---|
| 1 | **Permanent object IDs** ✔ | Every saved object has a GUID that survives renames, save/load and copy/paste, in both scene formats; old scenes get IDs when opened | 1 week |
| 2 | **Rust plugins** ✔ | A `kronos` Rust crate wraps `plugin/kronos_plugin.h` safely; a template Rust plugin builds with `cargo` and loads in Studio (in-process and sandboxed) | 1 week |
| 3 | **Python game scripts** | An embedded PocketPy runs `.py` scripts next to Luau with the same memory/time limits and security levels, and a first slice of the `world`/`events` API | 2–3 weeks |
| 4 | **Tighter script limits** ✔ | A per-game script memory budget plus a smaller per-script limit (today 256 MB per script), with a warning before the hard stop | 2–3 days |
| 5 | **Split the Renderer** | `Renderer.cpp` is split into eight files by job (device, shadows, resources, pipelines, post-process setup and targets, passes, frame) with no logic change (12 Oct 2026). `Renderer.hpp` (2,100 lines, one class) is still whole | Part 1 done; header split left |
| 6 | **Server program without graphics** | `kronos_server` runs published games with no window, GPU, ImGui or SDL library (Linux, 12 Oct 2026; see [SERVER.md](SERVER.md)). Still to do: Windows build, and splitting the simulation out of `Application` so the graphics code isn't compiled in | Part 1 done |
| 7 | **Games out of the engine library** | Broken Bones, TNT Wars, Mining Sim, Despair and the trailer code move out of `engine_core` into their own targets; the network protocol carries generic messages instead of game ones (`FireWeapon`, `SelectClass`, ...) | 1–2 weeks |
|  | *Size check (12 Oct 2026):* about 15,000 lines in `tntwars/` (9,200), `despair/`, `miningsim/`, `trailer/`. The games are leaves; the coupling is `Application.hpp` (23 game includes and the members behind them, about 240 `tntwars::`/`despair::`/`miningsim::`/`trailer::` uses in `Application.cpp`), `main.cpp`, `NetworkSession.hpp` (`TntWarsMatch`) and `ProceduralMaterials.hpp`. Plan: a small `GameMode` interface (`core/GameMode.hpp`) that `Application` holds, one game at a time, each checked on screen. **Despair is done** (12 Oct 2026): `despair/DespairGameMode.*` in its own `kronos_despair` library; `Application` has no Despair code left. Left: TNT Wars (`NetworkSession.hpp` `TntWarsMatch`, `ProceduralMaterials.hpp` `MapMaterial`), Mining Sim, trailer. | |
| 8 | **One version number** ✔ | `engine/CMakeLists.txt` `project(... VERSION ...)` is the single source; `KronosVersion.hpp` and the installer are generated from it | 1 day |
| 9 | **Moderation data on the backend** | Reports and moderation logs go to the backend database, not files next to the game server | 3–5 days |

### Done
- **Step 3, Python game scripts — first slice** (11 Oct 2026): a game's `Scripts/Main.py` runs on PocketPy 2.2.0 in the Player and on servers, with `world` (find, name, positions, impulses, velocity, time) and `events` (update, collision, unload). Each script has its own VM, a 64 MB memory limit, 8 ms per handler call, a native stack guard, and no files, OS, disk imports or raw memory; scripts that break a limit are stopped without taking the game down. Found and patched an upstream PocketPy bug (the garbage collector read uninitialised traceback data). Still to do: Studio Play and editor support, more of the API, fuzzing. See PYTHON_SCRIPTS.
- **Step 7, part 1** (11 Oct 2026): Broken Bones is its own library (`kronos_brokenbones`), linked only by the Player and the tests; `engine_core` no longer contains it. Part 2 (12 Oct 2026): Despair, TNT Wars, the render showcase and the trailer are out of `engine_core` too. `Application` talks to a `core::GameMode` interface (`core/GameMode.hpp`), TNT Wars' network messages go through a `net::GameRules` seam (`TntWarsNetRules`), and the games live in `kronos_despair` and `kronos_games`. `engine_core` has no game headers any more (`CaptureRig`, which Movie Mode's exporter uses, stays). Mining Sim never touched `Application`, so it needed no mode. This unblocks the server program (4.5 step 6).
- **Step 6, `kronos_server`, part 1** (12 Oct 2026): a headless server binary with no SDL, X11 or video-codec libraries, built from the same engine code (`server/ServerMain.cpp`, `server/SdlNull.cpp`). The Remote Check smoke test passes with it as the server (8/8), it stops on SIGTERM, and it runs with no display. See SERVER.md.
- **Step 5, Renderer split, part 1** (12 Oct 2026): `core/Renderer.cpp` (6,600 lines) is now `Renderer.cpp` (device, swapchain, shutdown) plus `core/render/RendererShadows|Resources|Pipelines|PostSetup|PostTargets|Passes|Frame.cpp`; the shared shader/layer helpers moved to `core/render/RendererInternal.hpp`. Code moved verbatim; tests 21330/21330, smoke 8/8, showcase checked on screen. The class declaration in `Renderer.hpp` is not split yet.
- **Step 2, Rust plugins** (11 Oct 2026): `sdk/rust/kronos` wraps `kronos_plugin.h` in safe Rust (a `Plugin` trait, `Host` for scene access and registering panels and importers with closures, `Ui` widgets, importer `Output`, channels, an `export_plugin!` macro, panics caught at the boundary). `templates/rust-plugin` is a Rust port of the C sample (Grid Snap panel and PGM-to-OBJ importer). The engine tests build it with `cargo` and run the same checks as the C sample, in-process and sandboxed; all pass. Apache 2.0 like the rest of the plugin kit. See PLUGIN_API "Rust".
- **Step 1, permanent object IDs** (11 Oct 2026): every saved object gets a random 128-bit id (`core::Guid`) that survives renames, save/load and undo, in both scene formats; Duplicate, Paste and `Clone()` give copies new ids, and old scenes get ids when opened. Parents are now found by id, which also fixes objects with the same name (many "Part"s) getting the wrong parent or missing their physics body after loading. Object links in Roblox properties are saved by id, so `WeldConstraint`, `Weld` and `Motor6D` now survive save/load, and Validate Project only warns about links to objects that can't be saved. See SCENE_SYSTEM "Scene file persistence".
- **Step 4, tighter script limits** (11 Oct 2026): each script now gets 64 MB (was 256 MB), and all of a game's scripts share a 256 MB budget; the shared Roblox server/client VMs count against the game budget as a whole. A script passing 80% of its limit, or the game passing 80% of its budget, prints one warning in the Output before anything fails; the warning re-arms once usage drops below 60%. Memory freed by unloaded scripts goes back to the game budget. Settable with `Scripting::setMaxMemoryBytesPerScript`/`setGameMemoryBudget`. Tested with small limits (warning, game budget stopping a script under its own limit, memory returned on unload).
- **Step 8, one version number** (11 Oct 2026): the version is set once, in `engine/CMakeLists.txt` (`project(... VERSION 4.3.1)` plus `KRONOS_VERSION_SUFFIX "-pre-beta"`). Configuring the build generates `core/KronosVersion.hpp` (from `KronosVersion.hpp.in`) and `kronos_version.iss`, which the Windows installer script includes, so the app, the update check, the packages and the installer can't disagree. A test checks the generated header agrees with itself.

Later in this track, once the above is done: a real render graph (passes
declare what they read and write), a command buffer between scripts and the
simulation, and Studio service interfaces for plugins.

---

## 4.6 — Prove it works

From the advisor's 80-idea review (12 October 2026), Milestone A: an imported
game passes documented gameplay checks, failures say what to fix, and repeat
runs catch regressions. Milestones B (Studio as a full creator tool), C
(platform: accounts, hosting, persistence) and D (browser play, AI help,
cloud Studio) come after.

| # | Step | Done when | Size |
|---|---|---|---|
| 1 | **Mesh shapes** ✔ | `SpecialMesh`, `BlockMesh`, `CylinderMesh` and `WedgePart` are drawn with their real shapes, and `Shape = Cylinder` lies along X | 1–2 days |
| 2 | **Compatibility Lab** ✔ | `kronos_compat --lab` plays each place with physics and two players for a set time, isolates crashes, reports errors with time and line, and compares against a saved baseline | 1 week |
| 3 | **Bigger test corpus** ✔ | More test places of the common kinds (simulator, combat, inventory, GUI-heavy), each with behaviour checks | 3–5 days |
| 4 | **GUI and images** ✔ | Images drawn in GUI; `TextBox`, `ScrollingFrame`, `UIGridLayout`, `UIStroke`, `UIGradient`, `UIScale` and friends work | 1–2 weeks |
| 5 | **Honest README** ✔ | The README's Roblox claims match ROBLOX_BRIDGE, with the main limits in one place | 1 hour |
| 6 | **Studio test with a server** ✔ | Studio starts a local server and 1–4 Player windows on the open scene, shows their logs and stops them (Roblox's Start Server + Players) | 2–3 days |
| 7 | **Server program without graphics** ✔ (Linux) | `kronos_server`, see 4.5 step 6 | 2–4 weeks |
| 8 | **Studio as a creator tool (Milestone B), first round** ✔ | Edit menu, View panel toggles, undo labels, Python and Rust in Studio, multi-object Inspector, Wedge in the Part menu, oldest-first Explorer, hidden panels remembered | 1 week |

### Done
- **ProximityPrompt** (13 Oct 2026): prompts show for the nearest enabled one in range, key press or hold fires `Triggered`, the server re-checks reach. See ROBLOX_BRIDGE "Tools".
- **UserInputService** (12 Oct 2026): `InputBegan`/`InputEnded`/`InputChanged` with `InputObject`s, `IsKeyDown`, `IsMouseButtonPressed`, `GetMouseLocation`, `GetMouseDelta`, `GetFocusedTextBox` in the Player and Studio Play, fed through `core/RobloxInput`. See ROBLOX_BRIDGE "UserInputService". `ContextActionService` is next.
- **Step 8, Studio first round** (12 Oct 2026): the Edit menu lists Cut/Copy/Paste/Duplicate/Delete/Select All/Group/Ungroup with shortcuts and shows `Undo <what>`; View toggles Explorer, Inspector, Script Editor, Stats and Scene Search and remembers hidden panels (a `.panels` file next to the layout); the Inspector edits every selected object at once (properties they all share; an amber name means their values differ; one undo step); the Part menu has Wedge; the Explorer lists oldest first. Python scripts (first line `#`) and Rust plugins (File > Rust Plugins...) work from Studio. Clients now see other players' avatars. Gizmo drags and the Windows side are not tested yet.
- **Step 1, mesh shapes** (12 Oct 2026): Roblox mesh objects change the shape of their part, `WedgePart`s are wedges (also inside unions), and cylinder parts lie along X like in Roblox (they stood upright before). The owner's obby went from 93% to 94% (215 of 259 objects understood). See ROBLOX_BRIDGE "Mesh shapes".
- **Step 2, Compatibility Lab** (12 Oct 2026): `kronos_compat --lab` plays each place headlessly with physics and test players, each in its own process, and reports script errors (time, script, line), deaths, parts falling out of the world, touches, instance counts, behaviour checks and missing GUI images. `--baseline` lists what got better or worse. See [COMPAT_LAB.md](COMPAT_LAB.md).
- **Step 3, bigger corpus** (12 Oct 2026): five new test places with checks files (simulator, combat arena, inventory, round system, GUI menu), 10 in total. The Lab found two gaps, now filled: Roblox **Tools** (equip, `Activate`, `Equipped`/`Unequipped`, pickup by touch, hotbar on keys 1–9, `RightGrip` weld) and `ClickDetector`. See ROBLOX_BRIDGE "Tools".
- **Step 4, GUI and images** (12 Oct 2026): `TextBox` (typing, focus, `FocusLost`), `ScrollingFrame` (wheel, clipping, automatic canvas), `UIGridLayout`, `UIStroke`, `UIGradient` (new `ColorSequence`/`NumberSequence` value types), `UIScale`, aspect/size/text-size constraints, and images from the game's folder (`rbxassetid://` ids via `assets/` or `asset_map.json`). Layout no longer needs graphics, so the Lab checks GUI too. See ROBLOX_BRIDGE "GUI".
- **Step 5, honest README** (12 Oct 2026): the README now says what works, how it's tested (the 10 test places and the Lab) and lists the big gaps in one place.
- **Step 6, Studio test with a server** (12 Oct 2026): Test tab → **Server** → 1–4 players saves the scene as a temporary game, starts `engine_runtime --server --headless` and one `--client` window per player (`--name Player1`, ...), and the Multiplayer Test window shows each one's state and log. Client windows launched with `--game` now get the Player's in-game menu, chat and Roblox GUI. A test starts a real server and two windowless players and checks they join and talk to the server. See [MULTIPLAYER_TEST.md](MULTIPLAYER_TEST.md).

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
- **Deterministic physics and rollback matches** (`core/PhysicsRollback`, `net/RollbackProtocol`, `net/RollbackNetSession`, see `docs/ROLLBACK.md`): Jolt is now built cross-platform deterministic and engine code no longer fuses multiply-adds, so the same inputs give bit-identical physics whatever the core count, and should across machines too (Jolt's cross-platform guarantee; only tested on one machine so far). Physics state can be snapshotted, restored and hashed, and contact events come out in a fixed order. `PhysicsRollback` runs GGPO-style rollback over the whole physics world: predicted remote inputs, snapshot restore and resimulation when a real input differs, and checksums of confirmed frames so a desync is caught within a few frames. Inputs travel in compact unreliable packets that repeat anything not yet acknowledged, and faster peers slow down slightly to stay level with slower ones. `engine_runtime --rollback-host N` / `--rollback-join <address>` play a peer-to-peer match: the world stays frozen until everyone joins, a joiner whose scene differs from the host's is stopped, then every player gets a physics character driven by a deterministic copy of the normal movement code (moving platforms are driven inside the simulation too). Matches can be recorded as replays, and replay tests check resimulation stays bit-identical with different thread counts. Tested with two peers over 20% packet loss and jitter (bit-identical to a no-latency reference), over real ENet with 10% loss, and with two Kronos Player windows on one machine. Rollback-aware Luau (match scripts that run inside the simulation) followed in 4.4.
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
