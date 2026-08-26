// studio entry point -- links engine_core, same as engine_runtime's
// main.cpp, per docs/ARCHITECTURE.md Principle 4 ("what you see in Studio
// is what ships"). See StudioApp.hpp for what this shell does and doesn't
// do yet.
//
// Kronos ("Windows Studio Launch Crash" -- Jay's issue): a packaged
// kronos-windows-x64 studio.exe opened and closed immediately with no
// error message. CrashReporter.hpp's own signal handler is real, honest
// Linux/glibc-only -- a no-op on Windows -- and the plain
// std::fprintf(stderr, ...) this file used to rely on for a clean
// initialize()-returned-false failure has no guaranteed visible surface
// on Windows either (no console attached at all for a double-clicked
// GUI-launched .exe, or a console-subsystem window that opens and closes
// with the process before anyone can read it -- either way, "no error
// message" is the honest, reproducible baseline, not a guess about which
// specific mechanism swallows it). This file now wraps startup in a real
// top-level try/catch and shows a real native SDL_ShowSimpleMessageBox
// popup on any fatal failure -- initialize() returning false, or an
// uncaught C++ exception -- instead of exiting silently. See
// StudioApp::lastInitError()'s own comment for where the initialize()
// failure text itself comes from, and core::executableDirectory()'s own
// comment for the actual root cause this session found and fixed
// underneath the crash-loop (a packaged Windows build could never find
// its own bundled shaders/assets at all, which is what was actually
// making initialize() return false in the first place).
#include <cstdio>
#include <exception>
#include <string>

#include <SDL2/SDL.h>

#include "core/CrashReporter.hpp"
#include "core/Logger.hpp"
#include "studio/StudioApp.hpp"

namespace {

// Real native error dialog -- SDL_ShowSimpleMessageBox is documented to
// work even if SDL_Init() was never called (or failed), which matters
// here: some of the failures this catches (e.g. Window::initialize()
// itself failing) happen before/during SDL's own video subsystem coming
// up. Also mirrors to stderr for the real environments where that *is*
// visible (a terminal launch, Linux, CI) -- this popup is additive, not
// a replacement for that.
void showFatalErrorDialog(const std::string& message) {
    std::fprintf(stderr, "studio: fatal: %s\n", message.c_str());
    // Kronos ("Fatal Init Diagnostics" -- Jay's Windows startup-crash
    // report): points whoever sees this dialog at the real, persistent
    // log file (see main()'s own Logger::instance().enableFileLogging()
    // call) -- the dialog text itself is already the full diagnosis, but
    // a tester reporting this back (Jay's own report was exactly a
    // paraphrase with no detail) benefits from knowing there's a real
    // file to attach/paste from instead of retyping what they saw.
    std::string withLogHint = message + "\n\nSee kronos_engine.log (next to this program) for full details.";
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Kronos Studio - Fatal Error", withLogHint.c_str(), nullptr);
}

} // namespace

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    // Kronos ("Fatal Init Diagnostics" -- Jay's Windows startup-crash
    // report): the real, persistent counterpart to the message box below
    // -- a dialog only helps if someone reads/screenshots it before
    // dismissing it, and says nothing at all about *why* to whoever
    // triages the report afterward. Enabled first, before anything else
    // (including installCrashReporter() just below), so even a failure
    // in Window::initialize() -- which happens before Studio has any UI
    // at all -- still gets its real diagnosis (see
    // Window.cpp's own classifySdlFailure()) written to a real file on
    // disk. A real, honest best-effort: if the working directory itself
    // isn't writable, this just silently stays off (stderr/the dialog
    // still work either way) rather than crashing Studio before it even
    // starts over a logging nicety.
    if (!engine::core::Logger::instance().enableFileLogging("kronos_engine.log")) {
        std::fprintf(stderr, "studio: could not open kronos_engine.log for writing -- continuing without a log file.\n");
    }

    // Kronos (Alpha Completion Checklist, "Crash & Error Telemetry" --
    // "Crash report file"): same real signal handler engine_runtime
    // installs -- see CrashReporter.hpp's own header comment. Real,
    // honest no-op on Windows (see this file's own header comment) --
    // the try/catch plus message-box dialog below is this platform's
    // real equivalent for the one failure shape a signal handler can't
    // help with anyway (a clean, non-crashing "initialize() returned
    // false").
    engine::core::installCrashReporter();

    try {
        engine::studio::StudioApp app;
        if (!app.initialize()) {
            std::string message = app.lastInitError();
            if (message.empty()) message = "StudioApp::initialize() failed for an unspecified reason.";
            showFatalErrorDialog(message);
            return 1;
        }

        app.run();
        app.shutdown();
        return 0;
    } catch (const std::exception& e) {
        showFatalErrorDialog(std::string("Unhandled exception: ") + e.what());
        return 1;
    } catch (...) {
        showFatalErrorDialog("Unhandled exception of unknown type.");
        return 1;
    }
}
