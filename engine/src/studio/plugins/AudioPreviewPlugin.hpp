#pragma once

#include <string>
#include <utility>
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
//
// Kronos ("Modular Executable Targets" -- kronos_audio's own dedicated
// workspace): drawPanel() now Begin()/End()s 5 separate, independently
// dockable windows -- "Audio Source" (load/play/metadata, previously the
// unnamed top section of one "Audio Previewer" window), "DSP Node Graph"
// (drawDspGraphSection(), unchanged), "Viseme Timeline"
// (drawLipSyncSection(), unchanged), and (Kronos "Kronos Audio Missing
// Features") the previously-missing "Waveform Inspector" (real PCM peaks,
// core::computeWaveformPeaks() over the real decoded buffer) and "Audio
// Track Mixer" (real per-channel gain/mute/solo wired to
// core::Audio::setSoundVolume()/setMasterVolume(), real static peak/RMS
// dBFS meters, core::computePeakDbfs()/computeRmsDbfs() over the same
// real buffers -- "static" because this engine's Audio API has no per-
// frame playback-cursor readback to drive a live-updating VU needle, see
// those functions' own header comments).
class AudioPreviewPlugin final : public IStudioPlugin {
public:
    [[nodiscard]] bool initialize();
    void shutdown();

    [[nodiscard]] const char* name() const override { return "Audio Previewer"; }
    [[nodiscard]] const char* category() const override { return "Assets"; }

    void drawPanel(core::ECS& ecs, core::EntityId selected, const std::vector<core::EntityId>& selectedEntities) override;

private:
    void drawAudioSourceWindow();
    void drawDspGraphWindow();
    void drawVisemeTimelineWindow();
    void drawWaveformInspectorWindow();
    void drawTrackMixerWindow();
    void drawDspGraphSection();
    void drawLipSyncSection();
    // Shared by both the Source and Processed sections of the Waveform
    // Inspector -- draws `peaks` (see core::computeWaveformPeaks()) as a
    // real min/max bar per bucket into a fixed-height child region, via
    // ImGui::GetWindowDrawList() the same way every ViewportPanel debug
    // overlay already draws world-space lines into its own window.
    void drawWaveformBars(const char* childId, const std::vector<std::pair<float, float>>& peaks, float heightPx);
    // One real channel strip (vertical fader + mute/solo + peak/RMS dBFS
    // readout) -- shared by the Track Mixer's Source/Processed/Master
    // strips rather than 3 independently-drifting copies of the same
    // layout. `gain` is the caller's own real state (this function
    // doesn't own any); `peakDbfs`/`rmsDbfs` are already-computed values
    // to display, not recomputed per frame (see computePeakDbfs()'s own
    // header comment on why this is a real, static-per-load snapshot).
    // `muted`/`soloed` are both nullptr for Master -- a master bus has no
    // meaningful "mute/solo of itself" the way a Source/Processed channel
    // does (passing a plain local bool instead of nullptr would silently
    // discard every click, since it'd reset to its initializer every
    // frame rather than being real, persisted member state).
    void drawMixerChannelStrip(const char* label, float& gain, bool* muted, bool* soloed, float peakDbfs,
                                float rmsDbfs);

    core::Audio audio_;
    core::SoundHandle loadedSound_ = core::kInvalidSoundHandle;

    char pathBuffer_[256] = "";
    std::string statusMessage_;
    core::AssetMetadata lastMetadata_;

    // Kronos ("Waveform Inspector" + "Audio Track Mixer"): the real,
    // decoded mono buffer behind the loaded source (populated on a
    // successful Load, alongside loadedSound_ -- see drawAudioSourceWindow()),
    // reduced immediately into peaks/dBFS/duration and then cleared +
    // shrink_to_fit'd -- a multi-minute clip is tens of megabytes of raw
    // float samples that nothing here reads again after that one-time
    // reduction (only the reduced peaks and this cached duration are
    // drawn every frame), so holding onto the whole decode would just be
    // a real, avoidable memory leak in a tool meant to preview many
    // files across a session. Empty/at their real "silence floor"/0
    // default before anything is loaded.
    uint32_t sourceSampleRate_ = 0;
    float sourceDurationSeconds_ = 0.0f;
    std::vector<std::pair<float, float>> sourceWaveformPeaks_;
    float sourcePeakDbfs_ = -100.0f;
    float sourceRmsDbfs_ = -100.0f;
    // Track Mixer's own Source-channel real state -- see
    // drawMixerChannelStrip()'s own comment for what each field drives.
    float sourceGain_ = 1.0f;
    bool sourceMuted_ = false;
    bool sourceSoloed_ = false;

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
    // Same "Waveform Inspector"/"Track Mixer" real state as the Source
    // fields above, populated on a successful Process instead of Load --
    // no processedSamples_ member of its own since the DSP graph's own
    // AudioDspProcessResult::samples (already a real local in
    // drawDspGraphSection()) is reduced into these directly and never
    // needs to outlive that one call, unlike the Source channel (which
    // has no other buffer to read from on every later frame).
    std::vector<std::pair<float, float>> processedWaveformPeaks_;
    float processedPeakDbfs_ = -100.0f;
    float processedRmsDbfs_ = -100.0f;
    float processedGain_ = 1.0f;
    bool processedMuted_ = false;
    bool processedSoloed_ = false;

    // Master bus -- real core::Audio::setMasterVolume(), applied on top
    // of every other channel's own gain the same way a real master fader
    // always sits above per-bus faders (see that method's own doc
    // comment). No mute/solo of its own -- see drawMixerChannelStrip()'s
    // own comment on `hasSolo`.
    float masterGain_ = 1.0f;

    char transcriptBuffer_[512] = "";
    std::vector<core::VisemeEvent> lastVisemeEvents_;
    std::string lipSyncStatusMessage_;
};

} // namespace engine::studio::plugins
