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
- **New app icons** (direction A, "Orbit K with badge") for Player, Studio, 3D Tools, Movie Maker and Audio: multi-size Windows `.ico`, Linux hicolor PNGs 16–512 and scalable SVG.

### Still to build

| Item | Estimate |
|---|---|
| Deploy the backend and verify publish → package → play end to end on staging | 2–3 days |
| Catalog follow-ups: per-version packages and rollback, size limits per account, a moderation queue before a game goes public, thumbnails/screenshots, server-side manifest validation | 2–3 weeks |
| Script editor, next round: go to definition, rename symbol, find in all scripts, multi-cursor, a breakpoint/step debugger wired to the Luau VM | 3–4 weeks |
| Shader graph compiling to SPIR-V (partly exists) | ~2 weeks |
| Node graph compiling to bytecode (visual scripting) | 3–4 weeks |
| Resource layer: reference-counted handles, async loading, bundle dependency tracking, hot-swapping meshes/textures/audio at runtime (spec §3) | 3–4 weeks |
| Zero-copy hot reload for native plugins | ~2 weeks |

---

## 4.2 — Scale

Engine internals so large worlds run well. This is mostly the ROLE & MANDATE spec.

| Item | Notes | Estimate |
|---|---|---|
| BVH for culling and raycasts | Dynamic AABB tree, refit per frame. Replaces per-object frustum tests. An octree only for very large static sets. | 2–3 weeks |
| Static batching | Merge unmoving meshes that share a material at scene load | 1–2 weeks |
| Dynamic batching / GPU instancing | Instanced draws for repeated runtime meshes | 1–2 weeks |
| Vulkan 1.4 feature tier | Variable rate shading, host image copy, compute shader derivatives, shader clock timing. Each is optional with a fallback, because many GPUs still lack some of them. | 3–5 weeks |
| Spatial world streaming | Cell-based load/unload around players, built on the 4.1 resource layer | 4–8 weeks |
| Deterministic physics + rollback in gameplay | Jolt is not cross-machine deterministic by default. This needs fixed-point or strict float settings, deterministic job ordering and replay tests. | 4–6 weeks |
| Plugin registry hardening | Stable C ABI, versioned registries for assets, memory channels and editor panels, and sandboxing for third-party modules | 3–4 weeks |
| Audio mixer hierarchy | Buses, sends, ducking and snapshots driven by gameplay state | 3–4 weeks |
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
