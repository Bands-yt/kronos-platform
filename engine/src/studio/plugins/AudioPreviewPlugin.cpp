#include "studio/plugins/AudioPreviewPlugin.hpp"

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
    ImGui::Begin("Audio Previewer");

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

    drawDspGraphSection();
    drawLipSyncSection();

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
