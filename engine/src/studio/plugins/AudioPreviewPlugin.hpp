#pragma once

#include <string>
#include <vector>

#include "core/AssetMetadata.hpp"
#include "core/Audio.hpp"
#include "core/AudioDspGraph.hpp"
#include "core/PhonemeLipSync.hpp"
#include "studio/IStudioPlugin.hpp"

namespace engine::studio::plugins {

// A real audio previewer -- owns its own core::Audio instance (a real
// miniaudio ma_engine) rather than waiting on a future Play Solo session,
// the same "owns its own subsystem instance" precedent
// studio::DebugConsolePanel already set for Scripting: StudioApp runs no
// Audio of its own (see its class comment), so this is the only way an
// actual Play button plays an actual sound in Studio today. Load decodes
// via core::Audio::loadSound() and reports real metadata (duration/
// sample rate/channel count, core::extractAssetMetadata()'s own
// miniaudio ma_decoder probe -- a separate, throwaway decode purely for
// inspection, not the one that ends up playing), Play fires it as a
// non-positional one-shot.
//
// Kronos ("Node-Based Audio DSP & Phoneme Lip-Sync" -- v0.4.0 Creator
// Suite): also the real host for a core::AudioDspGraph (Gain -> Filter
// -> Pitch Shift -> Slice, all real, live-editable parameters) and
// core::extractLipSyncVisemes() -- see AudioDspGraph.hpp/
// PhonemeLipSync.hpp's own class comments for exactly what each is (and
// isn't). "Process" decodes the loaded file to a real mono float
// buffer, runs it through the graph, and writes the real result to a
// new WAV file next to the source (core::encodeFloatMonoToWavFile()) --
// non-destructive per AudioDspGraph.hpp's own header comment: the
// original file is never touched, and re-clicking Process after
// changing a slider re-derives a fresh result rather than compounding
// onto the previous one.
class AudioPreviewPlugin final : public IStudioPlugin {
public:
    [[nodiscard]] bool initialize();
    void shutdown();

    [[nodiscard]] const char* name() const override { return "Audio Previewer"; }
    [[nodiscard]] const char* category() const override { return "Assets"; }

    void drawPanel(core::ECS& ecs, core::EntityId selected, const std::vector<core::EntityId>& selectedEntities) override;

private:
    void drawDspGraphSection();
    void drawLipSyncSection();

    core::Audio audio_;
    core::SoundHandle loadedSound_ = core::kInvalidSoundHandle;

    char pathBuffer_[256] = "";
    std::string statusMessage_;
    core::AssetMetadata lastMetadata_;

    // Real DSP graph, built once in initialize() with a fixed real
    // topology (Input -> Gain -> Filter -> Pitch Shift -> Slice ->
    // Output) -- the node ids below are that fixed topology's own real
    // ids, kept so the UI can read/write each node's live parameters
    // directly rather than re-discovering them by walking the graph
    // every frame.
    core::AudioDspGraph dspGraph_;
    int dspInputNode_ = 0;
    int dspGainNode_ = 0;
    int dspFilterNode_ = 0;
    int dspPitchNode_ = 0;
    int dspSliceNode_ = 0;
    core::SoundHandle processedSound_ = core::kInvalidSoundHandle;
    std::string dspStatusMessage_;

    char transcriptBuffer_[512] = "";
    std::vector<core::VisemeEvent> lastVisemeEvents_;
    std::string lipSyncStatusMessage_;
};

} // namespace engine::studio::plugins
