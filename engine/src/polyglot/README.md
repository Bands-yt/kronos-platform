# Polyglot ECS Architecture

Mixed state: some of this is real, tested, and now linked into `engine_core`;
some is still draft scaffolding. Don't assume either way from the filename
alone — check below.

## Real, implemented, tested, linked into `engine_core`

- `EventBus.hpp`/`.cpp` — lock-free SPSC `EventRing` (`tryPush`/`tryPop`),
  real acquire/release atomics, real fixed-size 64-byte payload.
- `UnifiedTypeSystem.hpp`/`.cpp` — `TypeRegistry`, real component field
  descriptor validation (byte-size/overflow/duplicate-name checks).
- `PackageRegistry.hpp`/`.cpp` — real hand-rolled manifest text format
  parser (`PackageManifestParser`), real DFS topological dependency
  resolver with cycle detection (`DependencyResolver`), real
  `PackageRegistry` wrapping both with validation. **Local resolution
  only** — there is no remote registry endpoint to fetch packages from;
  this parses/resolves/verifies manifests already present on disk.
- `VirtualFileSystem.hpp`/`.cpp` — real mount/resolve with priority-based
  shadowing and lexical path-traversal protection. Wired into
  `core::SceneFile::loadFromFile()`/`saveToFile()` as an optional
  resolution layer (see that class's own comment) for `.kronos` scene
  files specifically — not a general asset-streaming layer for every
  loader in the engine, which stays a separate, larger, unbuilt scope.

All four are covered by `tests/test_polyglot_core.cpp` (117 checks) and
have been run clean under AddressSanitizer+UndefinedBehaviorSanitizer.

## Still draft scaffolding — not wired into the build, not implemented

- `CrossLanguageDebugger.hpp` — unified breakpoint/step protocol
- `HotReload.hpp` — live component/system swap across runtimes

Pure interface sketches for review only. Every function body is either
missing or a `// TODO`.

## Why these two stay stubs

This engine's actual multi-language surface today is Luau only
(`core/Scripting.hpp`, `core/Script*Api.hpp`). Rust/WASM and TypeScript
runtimes don't exist in this codebase — these two headers assume a second
language host to debug/hot-reload, and there isn't one to build against.
Bridging Luau itself to `EventBus`/`TypeRegistry` was considered and
deliberately not built: `core::Scripting` already has a real, working
event system (`events.onUpdate`/`events.onCollision`, fired from
`Application.cpp`'s own hooks) with real callers; routing Luau through
`EventRing` too would stand up a second, parallel event system with
nothing using it, not an integration.

Building either stub for real means standing up that second host runtime
first. Expect the interface to change once one actually exists to design
against.
