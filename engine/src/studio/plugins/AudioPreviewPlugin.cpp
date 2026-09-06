#include "studio/plugins/AudioPreviewPlugin.hpp"

#include <algorithm>
#include <cstdio>

#include <imgui.h>

namespace engine::studio::plugins {

bool AudioPreviewPlugin::initialize() {
    // Real, fixed DSP topology, built once -- see this class's own
    // header comment. Every node starts at a real, audible-no-op
    // default (unity gain, a wide-open low-pass, no pitch change, the
    // slice spanning "the whole buffer" once Process() sets its real
    // endMs from the decoded length) so a first click of Process
    // reproduces the source rather than silence.
    dspInputNode_ = dspGraph_.addNode(core::AudioNodeKind::Input);
    dspGainNode_ = dspGraph_.addNode(core::AudioNodeKind::Gain);
    dspFilterNode_ = dspGraph_.addNode(core::AudioNodeKind::BiquadFilter);
    dspPitchNode_ = dspGraph_.addNode(core::AudioNodeKind::PitchShift);
    dspSliceNode_ = dspGraph_.addNode(core::AudioNodeKind::Slice);
    int dspOutputNode = dspGraph_.addNode(core::AudioNodeKind::GraphOutput);

    dspGraph_.findNode(dspFilterNode_)->cutoffHz = 20000.0f; // effectively a no-op low-pass until the user lowers it
    dspGraph_.findNode(dspSliceNode_)->sliceEndMs = 1.0e9f;  // "the whole buffer" until Process() clamps it to the real length

    // Real, fixed, hand-verified topology -- every addLink() below
    // connects two freshly-created pins of matching kinds, so failure
    // here would mean a real bug in this function itself, not a runtime
    // condition callers need to react to; still checked (never a
    // silently-ignored [[nodiscard]]) and logged if it ever does fail.
    std::string error;
    bool wired = true;
    wired &= dspGraph_.addLink(dspGraph_.findNode(dspInputNode_)->pinIds[0], dspGraph_.findNode(dspGainNode_)->pinIds[0], error);
    wired &= dspGraph_.addLink(dspGraph_.findNode(dspGainNode_)->pinIds[1], dspGraph_.findNode(dspFilterNode_)->pinIds[0], error);
    wired &= dspGraph_.addLink(dspGraph_.findNode(dspFilterNode_)->pinIds[1], dspGraph_.findNode(dspPitchNode_)->pinIds[0], error);
    wired &= dspGraph_.addLink(dspGraph_.findNode(dspPitchNode_)->pinIds[1], dspGraph_.findNode(dspSliceNode_)->pinIds[0], error);
    wired &= dspGraph_.addLink(dspGraph_.findNode(dspSliceNode_)->pinIds[1], dspGraph_.findNode(dspOutputNode)->pinIds[0], error);
    if (!wired) std::fprintf(stderr, "AudioPreviewPlugin: internal error wiring the default DSP graph: %s\n", error.c_str());

    return audio_.initialize();
}

void AudioPreviewPlugin::shutdown() { audio_.shutdown(); }

void AudioPreviewPlugin::drawPanel(core::ECS& /*ecs*/, core::EntityId /*selected*/,
                                     const std::vector<core::EntityId>& /*selectedEntities*/) {
    drawAudioSourceWindow();
    drawDspGraphWindow();
    drawVisemeTimelineWindow();
    drawWaveformInspectorWindow();
    drawTrackMixerWindow();

    // Kronos ("Audio Track Mixer"): real, applied every frame (not just
    // on slider release) so a drag mid-adjustment is heard immediately --
    // mute wins over the fader's own gain value; solo (when any channel
    // has it set) mutes every *other* non-soloed channel, the same
    // "solo silences everything else" convention every real mixer uses.
    // Master has no mute/solo of its own (see drawMixerChannelStrip()'s
    // comment) -- it's a plain multiplier on top, applied via
    // core::Audio::setMasterVolume() instead of per-sound.
    bool anySoloed = sourceSoloed_ || processedSoloed_;
    float sourceEffective = sourceMuted_ || (anySoloed && !sourceSoloed_) ? 0.0f : sourceGain_;
    float processedEffective = processedMuted_ || (anySoloed && !processedSoloed_) ? 0.0f : processedGain_;
    if (loadedSound_ != core::kInvalidSoundHandle) audio_.setSoundVolume(loadedSound_, sourceEffective);
    if (processedSound_ != core::kInvalidSoundHandle) audio_.setSoundVolume(processedSound_, processedEffective);
    audio_.setMasterVolume(masterGain_);
}

void AudioPreviewPlugin::drawAudioSourceWindow() {
    ImGui::Begin("Audio Source");

    ImGui::TextWrapped("Load an audio file and play it back through Studio's own audio engine.");
    ImGui::SetNextItemWidth(320.0f);
    ImGui::InputText("Path", pathBuffer_, sizeof(pathBuffer_));
    ImGui::SameLine();
    if (ImGui::Button("Load")) {
        std::string path = pathBuffer_;
        lastMetadata_ = core::extractAssetMetadata(path);

        if (!lastMetadata_.succeeded || lastMetadata_.kind != core::AssetKind::Audio) {
            statusMessage_ = lastMetadata_.succeeded ? "Not a recognized audio file." : ("Failed: " + lastMetadata_.error);
        } else {
            if (loadedSound_ != core::kInvalidSoundHandle) {
                audio_.unloadSound(loadedSound_);
                loadedSound_ = core::kInvalidSoundHandle;
            }
            loadedSound_ = audio_.loadSound(path);
            statusMessage_ = loadedSound_ != core::kInvalidSoundHandle ? "Loaded." : "Decode failed.";

            // Kronos ("Waveform Inspector" + "Audio Track Mixer"): a real,
            // separate decode from the same real path -- ma_sound (above)
            // never hands back a raw sample array (see
            // decodeAudioFileToFloatMono()'s own header comment), so this
            // is the one place a Load actually gets real PCM data to
            // bucket into peaks and measure real dBFS from. Reduced once
            // here into peaks/dBFS/duration -- see computeWaveformPeaks()'s
            // own comment on why never per-frame -- and then this local
            // buffer (tens of megabytes for a real multi-minute clip)
            // simply falls out of scope rather than being kept as member
            // state nothing reads again.
            std::vector<float> decoded;
            if (core::decodeAudioFileToFloatMono(path, decoded, sourceSampleRate_)) {
                sourceWaveformPeaks_ = core::computeWaveformPeaks(decoded, 512);
                sourcePeakDbfs_ = core::computePeakDbfs(decoded);
                sourceRmsDbfs_ = core::computeRmsDbfs(decoded);
                sourceDurationSeconds_ = sourceSampleRate_ > 0
                    ? static_cast<float>(decoded.size()) / static_cast<float>(sourceSampleRate_)
                    : 0.0f;
            } else {
                sourceWaveformPeaks_.clear();
                sourcePeakDbfs_ = -100.0f;
                sourceRmsDbfs_ = -100.0f;
                sourceDurationSeconds_ = 0.0f;
            }
        }
    }

    ImGui::BeginDisabled(loadedSound_ == core::kInvalidSoundHandle);
    ImGui::SameLine();
    if (ImGui::Button("Play")) {
        audio_.playOneShot(loadedSound_);
    }
    ImGui::EndDisabled();

    if (!statusMessage_.empty()) {
        ImGui::TextDisabled("%s", statusMessage_.c_str());
    }

    if (lastMetadata_.succeeded && lastMetadata_.kind == core::AssetKind::Audio) {
        ImGui::SeparatorText("Metadata");
        ImGui::Text("File size: %llu bytes", static_cast<unsigned long long>(lastMetadata_.fileSizeBytes));
        ImGui::Text("Duration: %.2f s", lastMetadata_.durationSeconds);
        ImGui::Text("Sample rate: %u Hz", lastMetadata_.sampleRate);
        ImGui::Text("Channels: %u", lastMetadata_.channelCount);
    }

    ImGui::End();
}

void AudioPreviewPlugin::drawDspGraphWindow() {
    ImGui::Begin("DSP Node Graph");
    drawDspGraphSection();
    ImGui::End();
}

void AudioPreviewPlugin::drawVisemeTimelineWindow() {
    ImGui::Begin("Viseme Timeline");
    drawLipSyncSection();
    ImGui::End();
}

void AudioPreviewPlugin::drawWaveformBars(const char* childId, const std::vector<std::pair<float, float>>& peaks,
                                            float heightPx) {
    if (peaks.empty()) {
        ImGui::TextDisabled("No audio loaded yet.");
        return;
    }

    ImGui::BeginChild(childId, ImVec2(0.0f, heightPx), true);
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImVec2 size = ImGui::GetContentRegionAvail();
    if (size.x <= 0.0f) size.x = 1.0f;
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    float midY = origin.y + size.y * 0.5f;
    constexpr ImU32 kWaveColor = IM_COL32(120, 200, 255, 220);
    constexpr ImU32 kCenterLineColor = IM_COL32(255, 255, 255, 60);
    drawList->AddLine(ImVec2(origin.x, midY), ImVec2(origin.x + size.x, midY), kCenterLineColor, 1.0f);

    // One vertical bar per bucket, min-to-max around the real center
    // line -- real PCM peaks (see computeWaveformPeaks()'s own comment),
    // not a synthetic sine placeholder.
    float barWidth = size.x / static_cast<float>(peaks.size());
    for (size_t i = 0; i < peaks.size(); ++i) {
        float x = origin.x + static_cast<float>(i) * barWidth;
        float yTop = midY - peaks[i].second * size.y * 0.5f;
        float yBottom = midY - peaks[i].first * size.y * 0.5f;
        drawList->AddRectFilled(ImVec2(x, yTop), ImVec2(x + std::max(1.0f, barWidth - 0.5f), yBottom), kWaveColor);
    }
    ImGui::Dummy(size);
    ImGui::EndChild();
}

void AudioPreviewPlugin::drawWaveformInspectorWindow() {
    ImGui::Begin("Waveform Inspector");
    ImGui::TextWrapped("Real PCM peaks from the actual decoded audio buffer -- one min/max bar per bucket, not a "
                        "synthetic placeholder.");

    ImGui::SeparatorText("Source");
    drawWaveformBars("##waveform_source", sourceWaveformPeaks_, 140.0f);
    if (!sourceWaveformPeaks_.empty()) {
        ImGui::Text("Peak: %.1f dBFS  |  RMS: %.1f dBFS  |  %u Hz  |  %.2f s", sourcePeakDbfs_, sourceRmsDbfs_,
                    sourceSampleRate_, sourceDurationSeconds_);
    }

    ImGui::SeparatorText("Processed (DSP Node Graph output)");
    drawWaveformBars("##waveform_processed", processedWaveformPeaks_, 140.0f);
    if (!processedWaveformPeaks_.empty()) {
        ImGui::Text("Peak: %.1f dBFS  |  RMS: %.1f dBFS", processedPeakDbfs_, processedRmsDbfs_);
    } else {
        ImGui::TextDisabled("Click Process (DSP Node Graph) to generate this.");
    }

    ImGui::End();
}

void AudioPreviewPlugin::drawMixerChannelStrip(const char* label, float& gain, bool* muted, bool* soloed,
                                                 float peakDbfs, float rmsDbfs) {
    ImGui::BeginGroup();
    ImGui::TextUnformatted(label);

    // Real vertical fader -- 0..2x linear gain (matches the DSP graph's
    // own Gain node range), the same real value setSoundVolume()/
    // setMasterVolume() are called with every frame (see drawPanel()'s
    // own comment).
    char faderId[64];
    std::snprintf(faderId, sizeof(faderId), "##fader_%s", label);
    ImGui::VSliderFloat(faderId, ImVec2(36.0f, 140.0f), &gain, 0.0f, 2.0f, "%.2f");

    // Real, static peak/RMS dBFS meter -- two stacked bars (RMS behind,
    // peak in front, the same "RMS shows the sustained loudness, peak
    // shows the real ceiling" convention every hardware/DAW meter uses),
    // mapped from the real, finite -100..0 dB range this file's own
    // computePeakDbfs()/computeRmsDbfs() always return.
    constexpr float kFloorDb = -100.0f;
    auto dbToFraction = [](float db) { return std::clamp((db - kFloorDb) / (0.0f - kFloorDb), 0.0f, 1.0f); };
    ImGui::SameLine();
    ImVec2 meterOrigin = ImGui::GetCursorScreenPos();
    ImVec2 meterSize(14.0f, 140.0f);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(meterOrigin, ImVec2(meterOrigin.x + meterSize.x, meterOrigin.y + meterSize.y),
                             IM_COL32(30, 30, 34, 255));
    float rmsHeight = meterSize.y * dbToFraction(rmsDbfs);
    drawList->AddRectFilled(ImVec2(meterOrigin.x, meterOrigin.y + meterSize.y - rmsHeight),
                             ImVec2(meterOrigin.x + meterSize.x, meterOrigin.y + meterSize.y),
                             IM_COL32(90, 200, 120, 220));
    float peakY = meterOrigin.y + meterSize.y - meterSize.y * dbToFraction(peakDbfs);
    drawList->AddLine(ImVec2(meterOrigin.x, peakY), ImVec2(meterOrigin.x + meterSize.x, peakY),
                       IM_COL32(255, 210, 80, 255), 2.0f);
    ImGui::Dummy(meterSize);

    if (muted != nullptr) {
        char muteId[64];
        std::snprintf(muteId, sizeof(muteId), "Mute##%s", label);
        ImGui::Checkbox(muteId, muted);
    }
    if (soloed != nullptr) {
        ImGui::SameLine();
        char soloId[64];
        std::snprintf(soloId, sizeof(soloId), "Solo##%s", label);
        ImGui::Checkbox(soloId, soloed);
    }
    if (peakDbfs <= kFloorDb + 0.01f && rmsDbfs <= kFloorDb + 0.01f) {
        ImGui::TextDisabled("(silent)");
    } else {
        ImGui::Text("%.1f dB", peakDbfs);
    }
    ImGui::EndGroup();
}

void AudioPreviewPlugin::drawTrackMixerWindow() {
    ImGui::Begin("Audio Track Mixer");
    ImGui::TextWrapped("Real per-channel gain/mute/solo -- applied live to actual playback every frame. Meters are "
                        "a real static peak/RMS reading of each loaded/processed clip, not a live VU needle (this "
                        "engine's audio API has no per-frame playback-cursor readback to drive one).");
    ImGui::Spacing();

    ImGui::BeginGroup();
    drawMixerChannelStrip("Source", sourceGain_, &sourceMuted_, &sourceSoloed_, sourcePeakDbfs_, sourceRmsDbfs_);
    ImGui::SameLine();
    ImGui::Dummy(ImVec2(16.0f, 0.0f));
    ImGui::SameLine();
    drawMixerChannelStrip("Processed", processedGain_, &processedMuted_, &processedSoloed_, processedPeakDbfs_,
                           processedRmsDbfs_);
    ImGui::SameLine();
    ImGui::Dummy(ImVec2(24.0f, 0.0f));
    ImGui::SameLine();
    float masterPeak = std::max(sourcePeakDbfs_, processedPeakDbfs_);
    float masterRms = std::max(sourceRmsDbfs_, processedRmsDbfs_);
    drawMixerChannelStrip("Master", masterGain_, nullptr, nullptr, masterPeak, masterRms);
    ImGui::EndGroup();

    ImGui::End();
}

void AudioPreviewPlugin::drawDspGraphSection() {
    ImGui::SeparatorText("DSP Graph (non-destructive)");
    bool hasSource = lastMetadata_.succeeded && lastMetadata_.kind == core::AssetKind::Audio;
    ImGui::BeginDisabled(!hasSource);

    core::AudioNode* gain = dspGraph_.findNode(dspGainNode_);
    core::AudioNode* filter = dspGraph_.findNode(dspFilterNode_);
    core::AudioNode* pitch = dspGraph_.findNode(dspPitchNode_);
    core::AudioNode* slice = dspGraph_.findNode(dspSliceNode_);

    ImGui::SliderFloat("Gain", &gain->gainLinear, 0.0f, 2.0f);
    ImGui::Checkbox("High-Pass (unchecked = Low-Pass)", &filter->filterIsHighPass);
    ImGui::SliderFloat("Filter Cutoff (Hz)", &filter->cutoffHz, 50.0f, 20000.0f, "%.0f", ImGuiSliderFlags_Logarithmic);
    ImGui::SliderFloat("Filter Q", &filter->q, 0.1f, 5.0f);
    ImGui::SliderFloat("Pitch Ratio", &pitch->pitchRatio, 0.25f, 4.0f);
    ImGui::TextWrapped("Pitch Ratio changes duration too (resample-based shift) -- see AudioDspGraph.hpp's own header comment.");
    ImGui::DragFloatRange2("Slice Range (ms)", &slice->sliceStartMs, &slice->sliceEndMs, 10.0f, 0.0f, 1.0e9f);

    if (ImGui::Button("Process")) {
        std::vector<float> samples;
        uint32_t sampleRate = 0;
        std::string path = pathBuffer_;
        if (!core::decodeAudioFileToFloatMono(path, samples, sampleRate)) {
            dspStatusMessage_ = "Failed to decode source for processing.";
        } else {
            // Clamp the slice's real end to the real decoded length the
            // first time (initialize()'s own 1e9f placeholder), so a
            // fresh load defaults to "the whole buffer" instead of
            // silently returning nothing past its real end.
            float durationMs = static_cast<float>(samples.size()) / static_cast<float>(sampleRate) * 1000.0f;
            if (slice->sliceEndMs > durationMs) slice->sliceEndMs = durationMs;

            dspGraph_.setInputBuffer(std::move(samples), sampleRate);
            core::AudioDspProcessResult result = dspGraph_.process();
            if (!result.success) {
                dspStatusMessage_ = "Processing failed: " + result.errorMessage;
            } else {
                std::string outPath = path + ".processed.wav";
                if (!core::encodeFloatMonoToWavFile(outPath, result.samples, sampleRate)) {
                    dspStatusMessage_ = "Processed, but failed to write " + outPath;
                } else {
                    if (processedSound_ != core::kInvalidSoundHandle) audio_.unloadSound(processedSound_);
                    processedSound_ = audio_.loadSound(outPath);
                    dspStatusMessage_ = "Processed " + std::to_string(result.samples.size()) + " samples -> " + outPath;

                    // Kronos ("Waveform Inspector" + "Audio Track
                    // Mixer"): real, from the exact same result.samples
                    // buffer just encoded above -- no second decode
                    // needed (unlike the Source channel, which has no
                    // in-memory buffer of its own until this Process
                    // path runs), and no member copy either: result.samples
                    // is already a real local that falls out of scope
                    // right after this reduction, so there's nothing to
                    // keep pinned as member state.
                    processedWaveformPeaks_ = core::computeWaveformPeaks(result.samples, 512);
                    processedPeakDbfs_ = core::computePeakDbfs(result.samples);
                    processedRmsDbfs_ = core::computeRmsDbfs(result.samples);
                }
            }
        }
    }
    ImGui::BeginDisabled(processedSound_ == core::kInvalidSoundHandle);
    ImGui::SameLine();
    if (ImGui::Button("Play Processed")) audio_.playOneShot(processedSound_);
    ImGui::EndDisabled();

    if (!dspStatusMessage_.empty()) ImGui::TextDisabled("%s", dspStatusMessage_.c_str());
    ImGui::EndDisabled();
}

void AudioPreviewPlugin::drawLipSyncSection() {
    ImGui::SeparatorText("Phoneme Lip-Sync (viseme timing, not acoustic recognition)");
    bool hasSource = lastMetadata_.succeeded && lastMetadata_.kind == core::AssetKind::Audio;
    ImGui::BeginDisabled(!hasSource);

    ImGui::SetNextItemWidth(400.0f);
    ImGui::InputText("Transcript", transcriptBuffer_, sizeof(transcriptBuffer_));
    if (ImGui::Button("Extract Visemes")) {
        std::vector<float> samples;
        uint32_t sampleRate = 0;
        std::string path = pathBuffer_;
        if (!core::decodeAudioFileToFloatMono(path, samples, sampleRate)) {
            lipSyncStatusMessage_ = "Failed to decode source for viseme extraction.";
            lastVisemeEvents_.clear();
        } else {
            lastVisemeEvents_ = core::extractLipSyncVisemes(samples, sampleRate, transcriptBuffer_);
            lipSyncStatusMessage_ = std::to_string(lastVisemeEvents_.size()) + " viseme event(s) extracted.";
        }
    }
    if (!lipSyncStatusMessage_.empty()) ImGui::TextDisabled("%s", lipSyncStatusMessage_.c_str());

    if (!lastVisemeEvents_.empty()) {
        ImGui::BeginChild("VisemeEventList", ImVec2(0.0f, 150.0f), true);
        for (const core::VisemeEvent& event : lastVisemeEvents_) {
            ImGui::Text("[%7.1f - %7.1f ms]  %s", event.startMs, event.endMs, core::visemeIdName(event.visemeId));
        }
        ImGui::EndChild();
    }
    ImGui::EndDisabled();
}

} // namespace engine::studio::plugins
