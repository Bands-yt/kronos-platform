# Polyglot ECS Architecture — draft scaffolding

Not wired into the build (no `CMakeLists.txt` references anything in this
directory). Pure interface sketches for review, not compiled, not tested,
not implemented. Every function body below either doesn't exist or is a
`// TODO` — nothing here does real work yet.

Five files, one per pillar:

- `UnifiedTypeSystem.hpp` — cross-language component struct mapping
- `CrossLanguageDebugger.hpp` — unified breakpoint/step protocol
- `HotReload.hpp` — live component/system swap across runtimes
- `PackageRegistry.hpp` — `kronos-pkg` mixed-language package bindings
- `EventBus.hpp` — lock-free cross-language event router

## Before any of this becomes real work

This engine's actual multi-language surface today is Luau only
(`core/Scripting.hpp`, `core/Script*Api.hpp`). Rust/WASM and TypeScript
runtimes don't exist in this codebase yet — these headers assume they do.
Building any one of these pillars for real means standing up that host
runtime first; the interface below is a starting sketch of the *shape*,
not a validated design. Expect it to change once a real second language
runtime exists to design against.
