# Kronos Roadmap

The next three releases after `4.0.0-beta`. Work that is already done locally
but held back from a public release is listed under the release that will
ship it. Estimates are for one engineer working full time, and they are
honest ones: most of the big items are weeks to months of work each, not hours.

| Release | Theme | Rough size |
|---|---|---|
| 4.1 | Creators: publishing, scripting, held-back fixes | 2–3 months |
| 4.2 | Scale: culling, batching, streaming, determinism, audio mixer | 4–6 months |
| 5.0 | Next-gen: GI, upscaling, web, collaboration, media tools | 12+ months, best split across several people |

## Already in the engine

These items from the AAA spec need hardening rather than building from scratch:

- **Data-oriented ECS.** Scenes are entt registries, so components are packed per type. There is no object-oriented scene graph to tear out.
- **Fixed-step accumulator.** `runtime/GameLoop` runs simulation and networking on fixed-rate accumulators decoupled from the render rate.
- **Plugin registry.** `core/PluginRegistry` and `NativePluginManager` load Studio and native plugins.
- **Script hot reload.** Saving a script reloads it on the next tick.
- **Rollback netcode foundation** (`net/RollbackSession`) and the **audio spectrogram**, both from 0.4.0.

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

| Item | Notes | Estimate |
|---|---|---|
| Keyframe-bound audio | Timeline (Movie Maker/trailer) nodes trigger audio events with sample-accurate scheduling | 2–3 weeks |
| Distributed compilation | Integrate `sccache`/`icecream` for C++ and a networked shader-compile cache rather than writing our own scheduler | 1–2 weeks |

---

## 5.0 — Next-gen

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

## Out of scope for now

- Rewriting the ECS or the game loop. Both already follow the data-oriented, fixed-step design the spec asks for, so the work goes into profiling and hardening instead.
- A home-grown distributed build scheduler. Existing tools cover it.
