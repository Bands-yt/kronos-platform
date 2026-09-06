# Multi-Language Scripting — Architecture Draft

Status: **draft, not implemented.** This document sketches how Python, Java,
and Rust could sit alongside Luau as gameplay/logic scripting languages. It
is meant to surface the real technical constraints and open decisions
before any code is written, not to specify a final design.

## What Luau's embedding actually gives us today

`core::Scripting` (`engine/src/core/Scripting.hpp`) is the baseline every
new language has to either match or explicitly fall short of:

- **One `lua_State` per script**, each with its own global table and a
  custom allocator enforcing a hard per-script memory ceiling.
- **A per-script time budget** enforced via Luau's interrupt hook — a
  runaway script gets killed, not the whole process.
- **`SecurityIdentity` levels** (`UserScript` / `CoreScript` /
  `StudioPlugin`, `core/ScriptSecurity.hpp`) gate which native bindings a
  given script can call, checked from the C++ side, unreachable from
  script code.
- **Coroutines inherit their creator's identity** — a script can't launder
  a low-privilege thread into a high-privilege one.

Any additional language needs an equivalent story for isolation, resource
limits, and privilege — not just "it can call `world.spawn()`".

## Per-language reality check

### Python (CPython)

- No built-in hard memory ceiling per interpreter. Sub-interpreters
  (`Py_NewInterpreterFromConfig`, CPython 3.12+) give separate globals but
  still share one process heap — a memory ceiling would need our own
  allocator hook (`PyMem_SetAllocator`) tracked per sub-interpreter, not
  something CPython gives us for free.
- The GIL means true parallel execution across scripts doesn't happen
  regardless of sub-interpreters (3.12's per-interpreter GIL is still
  experimental and build-config-gated) — matters for the time-budget
  story, since Luau's interrupt-hook approach assumes we can actually
  preempt.
- Real upside: sub-interpreters are the closest existing analogue to
  Luau's per-script `lua_State`, and CPython's C API is stable and
  well-documented.

### Java (JVM)

- No lightweight "one interpreter per script" primitive. Options are (a)
  one JVM per script (very heavy — a JVM's own startup/memory cost dwarfs
  a `lua_State`), or (b) one shared JVM with per-script `ClassLoader`
  isolation, which does NOT give hard memory/CPU ceilings without extra
  work (a custom `SecurityManager`-equivalent — `SecurityManager` itself
  is deprecated for removal as of JDK 17+, so this needs a real design,
  not a quick binding).
- Embedding means JNI (linking against a JVM shared library) or GraalVM's
  embedding API (heavier dependency, different licensing considerations
  to check before committing).
- Real upside: strong typing and existing tooling if a target audience
  specifically wants Java.

### Rust

- Rust is not naturally an "embed and interpret a script" language the
  way Luau/Python/Java are — it's ahead-of-time compiled. "Rust as a
  scripting language" means one of two genuinely different things:
  1. A Rust-*flavored* scripting VM (e.g. Rhai, Dyon) — real per-script
     isolation is achievable (these are designed for embedding), but
     scripts are then a Rust-like DSL, not actual Rust.
  2. Dynamically loaded compiled Rust plugins (`cdylib` + a stable C ABI)
     — this is real Rust, but it's a plugin model (compile, then load),
     not a live-edit scripting model, and a crashing/malicious plugin
     can take down the host process — no in-process sandboxing exists
     for arbitrary native code the way it does for a Lua VM.
- These two options solve different problems and probably shouldn't both
  be called "Rust scripting support" without disambiguating which one a
  given feature request means.

## Shared ECS/Instance binding layer

Luau's own binding layer (`world.*`, `events.*`, `ui.*` — see
`docs/LUA_API.md`) is hand-written per-function against the Luau C API.
None of it is reusable as-is for another language; each embedding needs
its own binding code registering the same *concepts* (spawn/destroy,
component get/set, event subscription) against that language's own
C-API/FFI conventions. A shared IDL (e.g. describing `world.spawn(kind,
transform)` once and generating bindings for each language) would reduce
duplication but is itself new infrastructure, not a shortcut.

## Recommended sequencing

Doing all three at once risks three shallow, unfinished integrations
instead of one real one. If this goes forward:

1. Pick **one** language first based on actual demand, not novelty.
2. Build its isolation/security story to Luau's own bar (memory ceiling,
   time budget, `SecurityIdentity`-equivalent) before wiring any ECS
   bindings — an unsandboxed script that can spawn entities is a real
   security regression, not a feature.
3. Bind a small, real subset of `world`/`events` (matching Phase 7's own
   Luau rollout shape) rather than the full API surface up front.

## Open questions before implementation starts

- Which language is actually wanted first, and why (creator demand,
  hiring pool, a specific integration)?
- Is "Rust scripting" meant as (a) a Rhai/Dyon-style DSL or (b) loadable
  native plugins? These have different security models and shipping
  binary size implications.
- What's the acceptable per-script memory/CPU budget for a non-Luau
  language, given none of them hit Luau's numbers for free?
- Does a shared script land in `CoreScript`/`StudioPlugin` trust tiers
  the same way, or does cross-language privilege need its own model?
