#pragma once

#include <array>
#include <functional>
#include <map>
#include <string>

#include "core/Audio.hpp"
#include "core/AudioMixer.hpp"
#include "studio/IStudioPlugin.hpp"

namespace engine::studio::plugins {

// Edits the game's mixer (buses, sends, ducking, snapshots) live, with
// meters, and saves it as mixer.kmixer next to the project, where the
// player picks it up.
class AudioMixerPlugin final : public IStudioPlugin {
public:
    AudioMixerPlugin(core::Audio& audio, std::function<std::string()> projectDirectory, std::function<bool()> playing);

    [[nodiscard]] const char* name() const override { return "Mixer"; }
    [[nodiscard]] const char* category() const override { return "Audio"; }

    void update(float dt, core::ECS& ecs, core::EntityId selected,
                const std::vector<core::EntityId>& selectedEntities) override;
    void drawPanel(core::ECS& ecs, core::EntityId selected, const std::vector<core::EntityId>& selectedEntities) override;

    [[nodiscard]] const core::MixerConfig& config() const { return config_; }
    [[nodiscard]] bool dirty() const { return dirty_; }
    [[nodiscard]] std::string mixerPath() const;
    bool save(std::string* error = nullptr);
    void reload();

private:
    struct Meter {
        float rmsDb = core::kMixerSilenceDb;
        float peakDb = core::kMixerSilenceDb;
        float duckDb = 0.0f;
    };
    struct TestSound {
        char path[512] = {};
        std::string bus = "Music";
        bool loop = true;
        core::SoundHandle handle = core::kInvalidSoundHandle;
        std::string loadedPath;
    };

    void apply();
    void drawToolbar();
    void drawStrips();
    void drawBusDetails();
    void drawSnapshots();
    void drawTestSounds();
    bool busCombo(const char* label, std::string& value, const std::string& exclude = {});

    core::Audio& audio_;
    std::function<std::string()> projectDirectory_;
    std::function<bool()> playing_;
    std::string loadedDirectory_ = "\x01";
    core::MixerConfig config_ = core::MixerConfig::defaults();
    std::string selectedBus_ = "Master";
    int selectedSnapshot_ = -1;
    float previewFade_ = 0.5f;
    bool dirty_ = false;
    std::string error_;
    std::string status_;
    char renameBuffer_[64] = {};
    std::string renameFor_;
    char snapshotName_[64] = {};
    int snapshotNameFor_ = -2;
    std::map<std::string, Meter> meters_;
    std::array<TestSound, 2> tests_;
};

} // namespace engine::studio::plugins
