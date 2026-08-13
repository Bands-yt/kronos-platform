#pragma once

namespace engine::core {

// Kronos (Alpha Completion Checklist, "Crash & Error Telemetry" -- "Crash
// report file"): installs a real POSIX signal handler (SIGSEGV/SIGABRT/
// SIGFPE/SIGILL/SIGBUS) that writes a real crash report file before the
// process dies -- signal name/number, a real glibc backtrace
// (backtrace_symbols_fd(), symbol names included since engine_runtime/
// studio both build RelWithDebInfo and keep symbols -- see this class's
// own .cpp comment for why that specific glibc function, not
// backtrace_symbols(), is used), and the most recent core::Logger
// ring-buffer entries (whatever the engine was last logging right before
// it crashed, often the single most useful clue toward *why*).
//
// Real, honest scope: Linux/glibc only (matches this codebase's own
// platform/LinuxWindow.cpp vs. platform/WindowsWindow.cpp split) -- a
// real, honest no-op on any other platform, not a silent crash-in-the-
// crash-handler. Also honest about signal-handler safety: strict
// async-signal-safety would forbid calling into core::Logger's own
// vector-returning API (it allocates) from inside the handler; this
// accepts that real, documented risk (the same pragmatic trade-off most
// real-world crash reporters make) in exchange for a report that
// actually contains useful recent context in the overwhelming majority
// of real crashes, which are not heap-corruption-induced.
void installCrashReporter();

} // namespace engine::core
