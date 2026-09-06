// Kronos ("Modular Executable Targets" -- v0.4.0 Creator Suite): boots
// StudioMode::Audio -- AudioPreviewPlugin (which already owns the real
// node-based core::AudioDspGraph and core::PhonemeLipSync UI sections
// internally -- see that plugin's own drawDspGraphSection()/
// drawLipSyncSection(); the v0.4.0 brief's "AudioPreviewPlugin/DSPGraph"
// is one plugin, not two, corrected in StudioApp::StudioMode's own header
// comment) plus MovieModePlugin (registered in every mode -- same
// comment). See StandaloneAppMain.hpp for the shared startup body.
#include "studio/apps/StandaloneAppMain.hpp"

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    return engine::studio::apps::runStandaloneApp(engine::studio::StudioApp::StudioMode::Audio, "kronos_audio.log",
                                                   "Kronos Audio");
}
