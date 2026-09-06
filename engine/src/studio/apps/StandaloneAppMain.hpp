#pragma once

#include "studio/StudioApp.hpp"

namespace engine::studio::apps {

// Kronos ("Modular Executable Targets" -- v0.4.0 Creator Suite): the one
// real, shared entry-point body behind all 4 studio/apps/*.cpp mains
// (kronos_studio, kronos_3d_maker, kronos_movie_maker, kronos_audio) --
// the exact same "install the crash reporter, enable file logging, run
// StudioApp inside a try/catch, show a native SDL_ShowSimpleMessageBox
// dialog on any fatal failure" sequence studio/StudioMain.cpp already
// established for the original `studio` target, parameterized by
// StudioApp::StudioMode instead of hardcoding StudioMode::Full, plus a
// per-app log file name (so 4 apps running at once, or run back-to-back,
// don't clobber one shared kronos_engine.log) and dialog title (so a
// fatal-error popup says which of the 4 apps actually crashed).
// studio/StudioMain.cpp itself is left untouched, not rewritten to call
// this too -- it's the one, pre-existing, already-shipping entry point
// for the flagship `studio` target, and duplicating ~70 lines of
// boilerplate once here for the 4 *new* targets is a smaller, safer
// change than touching that working file to share it.
//
// Returns the real process exit code -- 0 on a clean shutdown, 1 on any
// fatal failure (initialize() returning false, or an uncaught
// exception), exactly matching StudioMain.cpp's own convention.
int runStandaloneApp(StudioApp::StudioMode mode, const char* logFileName, const char* dialogTitle);

} // namespace engine::studio::apps
