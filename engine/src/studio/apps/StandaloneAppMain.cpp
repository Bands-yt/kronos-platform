#include "studio/apps/StandaloneAppMain.hpp"

#include <cstdio>
#include <string>

#include <SDL2/SDL.h>

#include "core/CrashReporter.hpp"
#include "core/Logger.hpp"

namespace engine::studio::apps {

namespace {

// Same real native error dialog as studio/StudioMain.cpp's own
// showFatalErrorDialog() -- see that file's header comment for why
// SDL_ShowSimpleMessageBox specifically (works even before/without
// SDL_Init() having run).
void showFatalErrorDialog(const std::string& dialogTitle, const std::string& logFileName, const std::string& message) {
    std::fprintf(stderr, "%s: fatal: %s\n", dialogTitle.c_str(), message.c_str());
    std::string withLogHint = message + "\n\nSee " + logFileName + " (next to this program) for full details.";
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, (dialogTitle + " - Fatal Error").c_str(), withLogHint.c_str(),
                              nullptr);
}

} // namespace

int runStandaloneApp(StudioApp::StudioMode mode, const char* logFileName, const char* dialogTitle) {
    if (!engine::core::Logger::instance().enableFileLogging(logFileName)) {
        std::fprintf(stderr, "%s: could not open %s for writing -- continuing without a log file.\n", dialogTitle,
                     logFileName);
    }

    engine::core::installCrashReporter();

    try {
        StudioApp app;
        if (!app.initialize(mode)) {
            std::string message = app.lastInitError();
            if (message.empty()) message = "StudioApp::initialize() failed for an unspecified reason.";
            showFatalErrorDialog(dialogTitle, logFileName, message);
            return 1;
        }

        app.run();
        app.shutdown();
        return 0;
    } catch (const std::exception& e) {
        showFatalErrorDialog(dialogTitle, logFileName, std::string("Unhandled exception: ") + e.what());
        return 1;
    } catch (...) {
        showFatalErrorDialog(dialogTitle, logFileName, "Unhandled exception of unknown type.");
        return 1;
    }
}

} // namespace engine::studio::apps
