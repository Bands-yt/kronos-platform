# Contributing to Kronos

Thanks for testing Kronos during Alpha v1 — this is exactly the stage
where bug reports and feedback matter most. This guide covers the three
things most testers want to do: report a bug, share feedback, and try
your own Luau scripts.

## Reporting a bug or crash

Open a [new issue](../../issues/new/choose) using the **Bug Report**
template — it's set up to keep crash logs and performance notes
organized so they're actually actionable. Before filing, it helps to
know:

- **Console output** — `engine_runtime`/`studio` print real diagnostics
  to stdout/stderr as they run; include the tail of that output if
  something crashed or misbehaved.
- **Crash reports** — a real crash writes a `crash_report_*.txt` file
  next to the binary (see [docs/CRASH_TELEMETRY.md](docs/CRASH_TELEMETRY.md));
  attach it if one exists.
- **Repro steps** — what you did, what you expected, what actually
  happened. "It crashed" is much harder to fix than "walked into the
  DynamicBox, pressed E, and it crashed on the next Interact."

If you hit a build problem rather than a runtime bug, check
[docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md) first — it covers
the common CMake/dependency issues before you file anything.

## General feedback

Not every note is a bug — performance impressions, UI friction, things
that felt confusing or missing. Feel free to open an issue for these
too; there's no wrong way to flag something that didn't feel right.
Performance feedback is especially useful with real numbers: the FPS
counter/profiler overlay (`F3` in `studio`, the periodic FPS line in
`engine_runtime`'s console output) gives you something concrete to
quote.

## Testing custom Luau scripts

Kronos ships a real, sandboxed embedded Luau runtime — every gameplay
script and Studio plugin runs in its own per-script VM with real
memory/CPU budgets, not a shared global environment. If you want to
poke at it:

- Start with [docs/LUA_CREATOR_EXPERIENCE.md](docs/LUA_CREATOR_EXPERIENCE.md)
  for the guided tour, and [docs/LUA_API.md](docs/LUA_API.md) for the
  full, current binding reference (`world.*`, `events.*`, `network.*`,
  `avatar.*`, `ui.*`).
- The default showcase world's own script
  (`games/DefaultWorld/Scripts/Main.lua`) is a real, working example —
  spawning dynamic physics objects, applying impulses, and handling a
  real Interact input hook. Copy it as a starting point.
- To try a script in your own game: copy `games/DefaultWorld` (or any
  folder under `games/`) as a template, edit `Scripts/Main.lua`, and
  launch it from the Game Catalogue — the engine reloads a changed
  script the next time that game loads.
- Found a binding that's missing, or one that behaves unexpectedly?
  That's exactly the kind of Alpha feedback worth an issue — include
  the script snippet that triggered it.

## A note on scope

Kronos' [LICENSE](LICENSE) is All Rights Reserved — this repo is open
for viewing, evaluation, and testing, not for external code
contributions (pull requests) at this stage. If that changes, this file
will say so.
