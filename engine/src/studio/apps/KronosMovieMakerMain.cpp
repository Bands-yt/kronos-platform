// Kronos ("Modular Executable Targets" -- v0.4.0 Creator Suite): boots
// StudioMode::MovieMaker -- MovieModePlugin + TrailerPanel, the cinematic
// authoring surface (6-track sequencer, camera-rail Bezier editor,
// offline exporter -- see plugins::MovieModePlugin's own class comment).
// See StandaloneAppMain.hpp for the shared startup body.
#include "studio/apps/StandaloneAppMain.hpp"

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    return engine::studio::apps::runStandaloneApp(engine::studio::StudioApp::StudioMode::MovieMaker,
                                                   "kronos_movie_maker.log", "Kronos Movie Maker");
}
