// Kronos ("Modular Executable Targets" -- v0.4.0 Creator Suite): boots
// StudioMode::ThreeDMaker -- MaterialPlugin + MeshCsgWindowPlugin (the
// v0.4.0 brief's "MeshCsgPlugin", corrected to its real name -- see
// StudioApp::StudioMode's own header comment) plus MovieModePlugin
// (registered in every mode -- see that same comment for why). See
// StandaloneAppMain.hpp for the shared startup body.
#include "studio/apps/StandaloneAppMain.hpp"

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    return engine::studio::apps::runStandaloneApp(engine::studio::StudioApp::StudioMode::ThreeDMaker,
                                                   "kronos_3d_maker.log", "Kronos 3D Maker");
}
