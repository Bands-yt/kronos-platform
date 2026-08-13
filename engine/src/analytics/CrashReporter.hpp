#pragma once

#include <string>

namespace engine::analytics {

// docs/ARCHITECTURE.md §13: "Crash reporting integrates each platform's
// mandated crash-reporter hook where certification requires it (consoles
// generally do)."
//
// install() wires a real POSIX signal handler (SIGSEGV/SIGABRT/SIGFPE)
// that writes a minimal crash marker before letting the process terminate
// normally -- genuinely functional, not a stub, since a basic
// signal-handler marker is cheap to get right using only async-signal-safe
// calls (write()/open(), not fprintf/iostream) and is worth having for
// real during development.
//
// What's NOT real: symbolication (turning addresses into function names/
// line numbers -- needs a separate out-of-process step reading debug
// info), upload to any crash-reporting backend, and every console
// platform's *mandated* crash-reporter hook (Xbox/PlayStation/Switch each
// require using their own SDK's crash-reporting API for certification,
// and those SDKs are NDA-gated -- see
// src/platform_adapters/adapters/README.md for why that's a structural
// limit here, not unfinished work). Windows' equivalent
// (SetUnhandledExceptionFilter) is also not implemented -- noted as the
// next platform this needs, not silently skipped.
class CrashReporter {
public:
    // Installs the signal handler (POSIX only -- see class comment).
    // `crashLogPath` is where the marker is appended; point this
    // somewhere real logs already live so it's actually found later.
    static void install(std::string crashLogPath);

    // Not implemented -- a real implementation reads whatever marker(s)
    // install() wrote on the *next* process start (the crashed process
    // can't safely do symbolication/upload from inside its own signal
    // handler) and forwards them to a real crash-reporting backend.
    static void processPendingCrashReports(const std::string& crashLogPath);

private:
    static void handleSignal(int signalNumber);
    static std::string logPath_;
};

} // namespace engine::analytics
