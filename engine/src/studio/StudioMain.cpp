// studio entry point -- links engine_core, same as engine_runtime's
// main.cpp, per docs/ARCHITECTURE.md Principle 4 ("what you see in Studio
// is what ships"). See StudioApp.hpp for what this shell does and doesn't
// do yet.
#include <cstdio>

#include "core/CrashReporter.hpp"
#include "studio/StudioApp.hpp"

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    // Kronos (Alpha Completion Checklist, "Crash & Error Telemetry" --
    // "Crash report file"): same real signal handler engine_runtime
    // installs -- see CrashReporter.hpp's own header comment.
    engine::core::installCrashReporter();

    engine::studio::StudioApp app;
    if (!app.initialize()) {
        std::fprintf(stderr, "studio: StudioApp::initialize failed.\n");
        return 1;
    }

    app.run();
    app.shutdown();
    return 0;
}
