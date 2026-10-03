#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "core/Audio.hpp"

namespace engine::brokenbones {

enum class Track { Title, Cliff, Freefall };
inline constexpr size_t kTrackCount = 3;

[[nodiscard]] const char* trackName(Track track);
[[nodiscard]] float trackBpm(Track track);
// One seamless mono loop at kSfxSampleRate: eight bars of pads, bass, arpeggio and drums.
[[nodiscard]] std::vector<float> synthesizeTrack(Track track);

// Plays at most one track at a time, crossfading whenever the requested track changes.
class MusicPlayer {
public:
    static constexpr float kFadeSeconds = 1.2f;

    bool load(core::Audio& audio, const std::string& directory);
    void play(std::optional<Track> track) { target_ = track; }
    void setVolume(float volume) { volume_ = volume; }
    // Temporary dip (pause menu, knocked out); 1 is full level.
    void setDuck(float duck) { duck_ = duck; }
    void update(float dt);
    void stop();
    [[nodiscard]] std::optional<Track> target() const { return target_; }
    [[nodiscard]] float level(Track track) const { return levels_[static_cast<size_t>(track)]; }

private:
    core::Audio* audio_ = nullptr;
    std::array<core::SoundHandle, kTrackCount> handles_{};
    std::array<float, kTrackCount> levels_{};
    std::array<bool, kTrackCount> playing_{};
    std::optional<Track> target_;
    float volume_ = 0.6f;
    float duck_ = 1.0f;
    float smoothedDuck_ = 1.0f;
};

} // namespace engine::brokenbones
