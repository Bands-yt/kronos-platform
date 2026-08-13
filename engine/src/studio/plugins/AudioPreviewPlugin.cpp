#include "studio/plugins/AudioPreviewPlugin.hpp"

#include <imgui.h>

namespace engine::studio::plugins {

bool AudioPreviewPlugin::initialize() { return audio_.initialize(); }

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

    ImGui::End();
}

} // namespace engine::studio::plugins
