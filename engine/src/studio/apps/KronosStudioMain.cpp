// Kronos ("Modular Executable Targets" -- v0.4.0 Creator Suite): the
// flagship standalone app -- StudioMode::Full, the exact same plugin
// roster the original `studio` target's own StudioMain.cpp boots. This
// target exists alongside `studio` (not a replacement for it) so the
// v0.4.0 brief's "kronos_studio" executable is a real, buildable binary
// under its own real name; see StandaloneAppMain.hpp's own comment for
// why the shared startup body lives there instead of here.
#include "studio/apps/StandaloneAppMain.hpp"

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    return engine::studio::apps::runStandaloneApp(engine::studio::StudioApp::StudioMode::Full, "kronos_studio.log",
                                                   "Kronos Studio");
}
