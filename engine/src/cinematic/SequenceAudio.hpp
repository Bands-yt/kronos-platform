#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "cinematic/Sequencer.hpp"
#include "core/Audio.hpp"

namespace engine::cinematic {

// Plays a sequence's Audio-track events at exact sample times. Events are scheduled a little
// ahead on the audio clock, so they don't depend on when the game frame happens to land.
class SequenceAudioPlayer {
public:
    using Resolver = std::function<core::SoundHandle(const std::string& payload)>;

    // Call once per frame after Sequence::advance. Stops pending cues when the sequence pauses
    // or jumps (scrub, setPlayhead).
    void update(const Sequence& sequence, core::Audio& audio, const Resolver& resolve);
    void stop(core::Audio& audio);

    void setLookahead(double seconds) { lookahead_ = seconds; }
    void setBus(std::string bus) { bus_ = std::move(bus); }
    [[nodiscard]] size_t scheduledCount() const { return scheduled_; }

private:
    void anchor(const Sequence& sequence, core::Audio& audio);
    void scheduleWindow(const Sequence& sequence, core::Audio& audio, const Resolver& resolve, double from, double to);

    bool running_ = false;
    uint64_t anchorFrame_ = 0;
    double anchorPlayhead_ = 0.0; // sequence time at anchorFrame_
    double played_ = 0.0;         // sequence seconds played since the anchor, loops unrolled
    double scheduledUntil_ = 0.0; // same units
    float lastPlayhead_ = 0.0f;
    double lookahead_ = 0.1;
    std::string bus_ = "SFX";
    size_t scheduled_ = 0;
};

// Loads the sound an Audio event's payload names: the payload as a path, then under each root,
// each with no extension, .wav, .ogg, .mp3 or .flac. Results, misses included, are cached.
class SoundPayloadCache {
public:
    void setRoots(std::vector<std::string> roots) { roots_ = std::move(roots); }
    core::SoundHandle resolve(core::Audio& audio, const std::string& payload);
    void forgetMissing();
    void clear(core::Audio& audio);
    [[nodiscard]] size_t loadedCount() const;

private:
    std::vector<std::string> roots_;
    std::unordered_map<std::string, core::SoundHandle> cache_;
};

} // namespace engine::cinematic
