#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "cinematic/CurveInterpolation.hpp"

namespace engine::cinematic {

// Multi-track sequencer data model.
//
// Separated from the ImGui editor on purpose: the timeline UI is a view
// of this, and keeping the model standalone means playback, evaluation
// and serialisation can be tested without an editor running.

enum class TrackKind : uint8_t {
    Camera = 0,          // rail parameter over time
    SkeletalAnimation = 1,
    Transform = 2,       // position/rotation/scale channels
    LightIntensity = 3,
    Audio = 4,
    ScriptTrigger = 5,   // fires once when the playhead crosses it
};

// A named scalar channel. Transform tracks use several (x/y/z);
// LightIntensity uses one. Keeping every track a bag of scalar channels
// means one curve implementation serves all of them.
struct TrackChannel {
    std::string name;
    std::vector<Keyframe> keys;
};

// A discrete event rather than a continuous value. Audio cues and script
// triggers are events: they happen at an instant, they do not interpolate.
struct TrackEvent {
    float timeSeconds = 0.0f;
    std::string payload; // clip name, script function, etc.
};

struct SequencerTrack {
    std::string name;
    TrackKind kind = TrackKind::Transform;
    // The entity/actor this drives. 0 means "not bound yet", which is a
    // real authoring state -- a track can exist before its subject does.
    uint64_t targetId = 0;
    bool muted = false;
    std::vector<TrackChannel> channels;
    std::vector<TrackEvent> events;

    [[nodiscard]] const TrackChannel* findChannel(const std::string& channelName) const;
    [[nodiscard]] TrackChannel& channel(const std::string& channelName); // creates on demand
};

// Frame rates the transport supports. Stored as an enum rather than a
// float so a timecode can never be computed against a rate that does not
// divide cleanly.
enum class SequenceFrameRate : uint8_t { Fps24 = 0, Fps30 = 1, Fps60 = 2 };

[[nodiscard]] int framesPerSecond(SequenceFrameRate rate);

// HH:MM:SS:FF. Frames are the remainder within the current second, so
// the frame field never reaches the frame rate itself.
[[nodiscard]] std::string formatTimecode(float timeSeconds, SequenceFrameRate rate);

// Snaps a time to the nearest whole frame. Scrubbing must land on frames
// or exported footage will not match what was previewed.
[[nodiscard]] float snapToFrame(float timeSeconds, SequenceFrameRate rate);

class Sequence {
public:
    [[nodiscard]] SequencerTrack& addTrack(const std::string& name, TrackKind kind);
    [[nodiscard]] const std::vector<SequencerTrack>& tracks() const { return tracks_; }
    [[nodiscard]] std::vector<SequencerTrack>& mutableTracks() { return tracks_; }
    void removeTrack(size_t index);

    void setFrameRate(SequenceFrameRate rate) { frameRate_ = rate; }
    [[nodiscard]] SequenceFrameRate frameRate() const { return frameRate_; }

    // Longest keyframe or event time across every track.
    [[nodiscard]] float durationSeconds() const;

    // Value of one channel at a time. Returns `fallback` when the track
    // or channel does not exist, rather than 0 -- a missing light
    // intensity track should leave the light alone, not black it out.
    [[nodiscard]] float sampleChannel(const std::string& trackName, const std::string& channelName, float timeSeconds,
                                       float fallback = 0.0f) const;

    // --- transport --------------------------------------------------------
    void play() { playing_ = true; }
    void pause() { playing_ = false; }
    [[nodiscard]] bool isPlaying() const { return playing_; }

    [[nodiscard]] float playheadSeconds() const { return playhead_; }
    // Snaps to a whole frame and clears crossed-event state, so scrubbing
    // backwards over a trigger lets it fire again.
    void setPlayhead(float timeSeconds);
    void stepFrames(int frames);

    // Loop region. Disabled when end <= start.
    void setLoopRegion(float startSeconds, float endSeconds);
    [[nodiscard]] bool loopEnabled() const { return loopEnd_ > loopStart_; }
    [[nodiscard]] float loopStart() const { return loopStart_; }
    [[nodiscard]] float loopEnd() const { return loopEnd_; }

    // Advances the playhead and returns every event crossed this step.
    //
    // Returning them rather than invoking a callback keeps this free of
    // any dependency on what an event *does*, and lets a test assert
    // exactly which fired.
    void advance(float deltaSeconds, std::vector<TrackEvent>& outFiredEvents);

    [[nodiscard]] std::string timecode() const { return formatTimecode(playhead_, frameRate_); }

private:
    std::vector<SequencerTrack> tracks_;
    SequenceFrameRate frameRate_ = SequenceFrameRate::Fps24;
    float playhead_ = 0.0f;
    bool playing_ = false;
    float loopStart_ = 0.0f;
    float loopEnd_ = 0.0f;
};

} // namespace engine::cinematic
