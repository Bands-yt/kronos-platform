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

// The authoring convention a TrackKind::Camera track uses to drive a rail
// parameter: a track named kCameraRailTrackName with a single scalar
// channel named kCameraRailChannelName, holding whatever real easing
// (Bezier, Cubic, Stepped...) an editor drew on its keys -- see
// TrackKind::Camera's own "rail parameter over time" comment above.
// Named constants rather than repeated string literals so
// studio::plugins::MovieModePlugin's own seedDefaultSequence() (the only
// real writer) and railParameterAtTime() below (the only real reader)
// can't silently drift apart into two different magic strings.
inline constexpr const char* kCameraRailTrackName = "Camera Rail";
inline constexpr const char* kCameraRailChannelName = "railT";

// Normalised [0,1] rail parameter for `timeSeconds`, sourced from the
// real authored easing on kCameraRailTrackName/kCameraRailChannelName
// when `sequence` has one. Sequence::sampleChannel() itself already
// returns a real, honest fallback when the track/channel is missing or
// has no keys (a muted track, or a bare CameraRail with no matching
// Sequence track authored at all) -- that fallback IS the plain linear
// timeSeconds/duration mapping, so there is exactly one code path here,
// not "curve, else linear" as two separate branches.
//
// The one real, single source of truth both studio::plugins::
// MovieModePlugin::railParameterAtPlayhead() (the live editor/viewport
// preview) and cinematic::runExportSchedule() (the Offline Export
// pipeline) call, so a creator's authored camera easing is what actually
// gets rendered, not silently discarded in favour of a raw linear scrub.
[[nodiscard]] float railParameterAtTime(const Sequence& sequence, float timeSeconds);

// Kronos ("Cinematic Camera Physics & Post-Processing Pipeline" -- Beta
// Roadmap Phase 3): the same real authoring convention as
// kCameraRailTrackName above, for post-processing tuning instead of a
// rail parameter. A track named kPostFxTrackName with up to three scalar
// channels (kBloomIntensityChannelName, kBloomThresholdChannelName,
// kExposureChannelName), each independently optional -- an unauthored
// channel real-falls back to whatever value the caller already had
// (see postFxAtTime()'s own comment), not an invented default, matching
// Sequence::sampleChannel()'s own "missing track/channel means leave it
// alone" contract.
//
// Deliberately does NOT expose focal length/aperture/ISO/focus distance
// here: those are already real, per-point-interpolated CameraRail state
// (see CameraRail.hpp's own RailPoint::focalLengthMm/aperture) -- a
// second, parallel Sequence-track path for the same physical parameters
// would just be two competing sources of truth for one real camera.
inline constexpr const char* kPostFxTrackName = "Post FX";
inline constexpr const char* kBloomIntensityChannelName = "bloomIntensity";
inline constexpr const char* kBloomThresholdChannelName = "bloomThreshold";
inline constexpr const char* kExposureChannelName = "exposure";

// core::Renderer's own real tuning knobs (see Renderer::setBloomSettings()/
// setExposure()), sampled from the sequencer instead of dialled in by
// hand. Deliberately excludes bloom soft-knee -- a minor tuning constant
// this codebase's own post-FX panels never animate, kept at whatever the
// renderer already has rather than adding a fourth rarely-used channel.
struct PostFxSample {
    float bloomIntensity = 0.6f; // matches Renderer's own class-default bloomIntensity_
    float bloomThreshold = 1.0f; // matches Renderer's own class-default bloomThreshold_
    float exposure = 1.0f;       // matches Renderer's own class-default exposure_
};

// Samples kPostFxTrackName's three channels at `timeSeconds`, falling
// back to `fallback`'s own fields (the caller's real current renderer
// state -- see trailer::CaptureRig::exportSequence()'s own save-before/
// restore-after use) per-channel independently when no authored curve
// exists for that specific one, so authoring only a bloom-intensity
// ramp doesn't also silently reset exposure to some invented constant.
[[nodiscard]] PostFxSample postFxAtTime(const Sequence& sequence, float timeSeconds, const PostFxSample& fallback);

} // namespace engine::cinematic
