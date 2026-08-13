#pragma once

#include <string>

#include "core/AssetMetadata.hpp"
#include "core/Audio.hpp"
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
class AudioPreviewPlugin final : public IStudioPlugin {
public:
    [[nodiscard]] bool initialize();
    void shutdown();

    [[nodiscard]] const char* name() const override { return "Audio Previewer"; }
    [[nodiscard]] const char* category() const override { return "Assets"; }

    void drawPanel(core::ECS& ecs, core::EntityId selected, const std::vector<core::EntityId>& selectedEntities) override;

private:
    core::Audio audio_;
    core::SoundHandle loadedSound_ = core::kInvalidSoundHandle;

    char pathBuffer_[256] = "";
    std::string statusMessage_;
    core::AssetMetadata lastMetadata_;
};

} // namespace engine::studio::plugins
