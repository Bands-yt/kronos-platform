# Kronos Alpha — Section 3: Crash & Error Telemetry

Status of the [Alpha Completion Checklist](ALPHA_COMPLETION_CHECKLIST.md)'s
Section 3. Verified with a full rebuild (`engine_runtime`, `studio`,
`engine_tests`) and the full test suite green (9523/9523 checks passing,
up from 9518 — 5 new tests).

## Audit finding

`core::Logger` (built in the Alpha Roadmap's Core Stability phase) was
only ever wired into gameplay `Script` error routing (Alpha Roadmap
Phase 7). Two real, adjacent gaps: `net::NetworkSession` still used raw
`std::fprintf` for its few logged events, and had **two completely
silent failure paths** (`hostServer()`/`connectToServer()` failing
propagated a bare `false` with no log anywhere); `studio::plugins::
ScriptedPlugin` and `studio::panels::DebugConsolePanel` both captured
script output into their own private, per-window buffers only, never
reaching the shared `Logger` Studio's own Engine Log tab (built in the
Tooling Layer phase) reads from. And no crash reporting existed at all —
a real crash produced nothing but whatever the OS's own default handler
does.

## Error categories

Real, free-form `core::Logger` categories (a `const char*`, not an enum —
matches `Logger.hpp`'s own deliberately small, un-enumerated design). The
real categories in active use as of this pass:

| Category | Source |
|---|---|
| `Script` | Gameplay `Script` components (`core::Application`) |
| `Console` | Studio's Debug Console REPL |
| `Plugin:<name>` | A scripted plugin's own output, tagged with its real manifest name so multiple loaded plugins stay distinguishable |
| `Network` | `net::NetworkSession` (connect/disconnect, stress test, and the two previously-silent failure paths) |
| `Crash` | The crash reporter itself (install confirmation) |
| `Mesh`, `Texture`, `RayTracingScene`, `OffscreenTarget`, `ThumbnailCapture` | Pre-existing (Alpha Roadmap Core Stability phase) |

## Script / Console / Plugin error routing

All three now classify the exact same way: a line starting with
`"compile error"` or `"runtime error"` (the real prefixes `core::
Scripting::loadAndRun()` itself already produces) routes to `logError()`;
everything else (including plain `print()`/`engine.log()` output) routes
to `logInfo()`. This is additive in both cases — `ScriptedPlugin::
outputLog_` and `DebugConsolePanel::history_` stay the real source of
truth for each panel's own window; the Logger call is a second, shared
destination, not a replacement.

## Networking error routing

`NetworkSession::initialize()`'s two previously-silent failure paths
(`hostServer()` and `connectToServer()` returning `false`) now real-log
at `Error` with the real port/address that failed. The three existing
`std::fprintf` calls (connect, disconnect, stress-test-started) converted
to `logInfo("Network", ...)`.

## Crash report file

**Built — `core::installCrashReporter()`** (`core/CrashReporter.{hpp,cpp}`):
a real POSIX signal handler (`SIGSEGV`/`SIGABRT`/`SIGFPE`/`SIGILL`/
`SIGBUS`/`SIGTRAP`) installed at the very start of both `main()`
functions (`main.cpp` and `StudioMain.cpp`), so even a startup crash is
caught. On a real crash, writes a real report file
(`crash_report_<timestamp>.txt`) containing:

- The real signal name/number.
- A real backtrace via glibc's `backtrace_symbols_fd()` — deliberately
  *not* `backtrace_symbols()`, which mallocs and is explicitly documented
  as unsafe to call from a signal handler; the `_fd` variant writes
  directly via `write()` instead.
- The most recent `core::Logger` ring-buffer entries (up to 50) — often
  the single most useful clue toward *why*, since it's whatever the
  engine was actually doing right before it crashed.

Then real-chains to the previous signal handler and re-raises, so the
OS's own default behavior (a real core dump if `ulimit` allows, a real
non-zero exit code) still happens — a crash reporter that swallowed the
crash instead of propagating it would be worse than not installing one.

**Honest, stated trade-off**: strict async-signal-safety would forbid
calling `Logger::recentEntries()` (it allocates) from inside the handler.
This accepts that real, documented risk — the same pragmatic call most
real-world crash reporters make — in exchange for a report that actually
contains useful recent context in the overwhelming majority of real
crashes, which aren't heap-corruption-induced. Linux/glibc only for now
(a real, honest no-op on any other platform, matching `platform/
LinuxWindow.cpp` vs. `platform/WindowsWindow.cpp`'s own existing split)
— not silently pretending to cover Windows too.

**Test**: `testCrashReporterWritesRealReportOnRealSignal` — a real,
isolated end-to-end test. `fork()`s a real child process that installs
the real handler and really calls `std::abort()`; the real parent waits
for it and confirms both that the child actually terminated via a
re-raised `SIGABRT` (the handler didn't swallow it) and that a real
report file appeared containing the real signal name and a real
backtrace section. `fork()` (rather than crashing the test process
itself) is what makes this safe to run inside the same suite as
everything else — crashing this process directly would have killed the
whole real test run, not just this one check.

## Summary

| Item | Status |
|---|---|
| Crash report file | Built — real signal handler, real backtrace, real recent-log context, real test via `fork()` |
| Error categories | Real, documented set in active use (table above) |
| Script error routing | Already real (Alpha Roadmap Phase 7); confirmed |
| Networking error routing | Built — 2 previously-silent failures now logged; 3 existing logs converted from raw `fprintf` |
| Plugin error routing | Built — `ScriptedPlugin` and `DebugConsolePanel` both now also route through the shared `Logger` |
