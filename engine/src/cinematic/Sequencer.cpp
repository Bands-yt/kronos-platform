#include "cinematic/Sequencer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace engine::cinematic {

namespace {
constexpr float kEpsilon = 1e-6f;
}

const TrackChannel* SequencerTrack::findChannel(const std::string& channelName) const {
    for (const TrackChannel& candidate : channels) {
        if (candidate.name == channelName) return &candidate;
    }
    return nullptr;
}

TrackChannel& SequencerTrack::channel(const std::string& channelName) {
    for (TrackChannel& candidate : channels) {
        if (candidate.name == channelName) return candidate;
    }
    channels.push_back(TrackChannel{channelName, {}});
    return channels.back();
}

int framesPerSecond(SequenceFrameRate rate) {
    switch (rate) {
        case SequenceFrameRate::Fps30: return 30;
        case SequenceFrameRate::Fps60: return 60;
        case SequenceFrameRate::Fps24:
        default: return 24;
    }
}

std::string formatTimecode(float timeSeconds, SequenceFrameRate rate) {
    const int fps = framesPerSecond(rate);
    // Negative times clamp: a timecode of -00:00:01:00 is not meaningful
    // and would format with a stray sign in the middle of the string.
    const float clamped = std::max(timeSeconds, 0.0f);

    const int totalFrames = static_cast<int>(std::floor(clamped * static_cast<float>(fps) + 0.5f));
    const int frames = totalFrames % fps;
    const int totalSeconds = totalFrames / fps;
    const int seconds = totalSeconds % 60;
    const int totalMinutes = totalSeconds / 60;
    const int minutes = totalMinutes % 60;
    const int hours = totalMinutes / 60;

    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d:%02d", hours, minutes, seconds, frames);
    return buffer;
}

float snapToFrame(float timeSeconds, SequenceFrameRate rate) {
    const float fps = static_cast<float>(framesPerSecond(rate));
    if (fps <= 0.0f) return timeSeconds;
    return std::floor(timeSeconds * fps + 0.5f) / fps;
}

SequencerTrack& Sequence::addTrack(const std::string& name, TrackKind kind) {
    SequencerTrack track;
    track.name = name;
    track.kind = kind;
    tracks_.push_back(std::move(track));
    return tracks_.back();
}

void Sequence::removeTrack(size_t index) {
    if (index >= tracks_.size()) return; // honest no-op rather than UB
    tracks_.erase(tracks_.begin() + static_cast<long>(index));
}

float Sequence::durationSeconds() const {
    float duration = 0.0f;
    for (const SequencerTrack& track : tracks_) {
        for (const TrackChannel& channel : track.channels) {
            if (!channel.keys.empty()) duration = std::max(duration, channel.keys.back().timeSeconds);
        }
        for (const TrackEvent& event : track.events) duration = std::max(duration, event.timeSeconds);
    }
    return duration;
}

float Sequence::sampleChannel(const std::string& trackName, const std::string& channelName, float timeSeconds,
                              float fallback) const {
    for (const SequencerTrack& track : tracks_) {
        if (track.name != trackName) continue;
        // A muted track reads as absent, so muting a light track leaves
        // the light at whatever the scene set rather than driving it to 0.
        if (track.muted) return fallback;
        const TrackChannel* channel = track.findChannel(channelName);
        if (channel == nullptr || channel->keys.empty()) return fallback;
        return sampleCurve(channel->keys, timeSeconds);
    }
    return fallback;
}

void Sequence::setPlayhead(float timeSeconds) {
    playhead_ = snapToFrame(std::max(timeSeconds, 0.0f), frameRate_);
}

void Sequence::stepFrames(int frames) {
    const float frameDuration = 1.0f / static_cast<float>(framesPerSecond(frameRate_));
    setPlayhead(playhead_ + static_cast<float>(frames) * frameDuration);
}

void Sequence::setLoopRegion(float startSeconds, float endSeconds) {
    loopStart_ = std::max(startSeconds, 0.0f);
    loopEnd_ = std::max(endSeconds, 0.0f);
}

void Sequence::advance(float deltaSeconds, std::vector<TrackEvent>& outFiredEvents) {
    // The caller owns the output vector, so a per-frame advance can reuse
    // one buffer instead of allocating a fresh vector every tick.
    outFiredEvents.clear();
    if (!playing_ || deltaSeconds <= 0.0f) return;

    const float previous = playhead_;
    float next = playhead_ + deltaSeconds;

    bool wrapped = false;
    if (loopEnabled() && next >= loopEnd_) {
        wrapped = true;
    }

    // Collect events in the half-open interval (previous, next]. Half-open
    // is what stops an event on an exact frame boundary firing twice when
    // one step ends where the next begins.
    auto collectInRange = [this, &outFiredEvents](float from, float to) {
        for (const SequencerTrack& track : tracks_) {
            if (track.muted) continue;
            if (track.kind != TrackKind::ScriptTrigger && track.kind != TrackKind::Audio) continue;
            for (const TrackEvent& event : track.events) {
                if (event.timeSeconds > from && event.timeSeconds <= to) outFiredEvents.push_back(event);
            }
        }
    };

    if (wrapped) {
        // Fire everything up to the loop end, then wrap and continue from
        // the loop start -- so an event near the end of a looped region
        // is not silently skipped on the frame that wraps.
        collectInRange(previous, loopEnd_);
        const float overshoot = next - loopEnd_;
        const float span = loopEnd_ - loopStart_;
        // A tiny loop region with a large delta could wrap many times;
        // fmod keeps this bounded rather than looping repeatedly.
        const float wrappedOffset = span > kEpsilon ? std::fmod(overshoot, span) : 0.0f;
        next = loopStart_ + wrappedOffset;
        collectInRange(loopStart_ - kEpsilon, next);
    } else {
        collectInRange(previous, next);
    }

    playhead_ = next;

    // Sort so callers receive events in the order they actually occurred,
    // regardless of which track they came from.
    std::sort(outFiredEvents.begin(), outFiredEvents.end(),
              [](const TrackEvent& a, const TrackEvent& b) { return a.timeSeconds < b.timeSeconds; });
}

float railParameterAtTime(const Sequence& sequence, float timeSeconds) {
    const float duration = std::max(sequence.durationSeconds(), 1e-4f);
    const float linearFallback = std::clamp(timeSeconds / duration, 0.0f, 1.0f);
    const float sampled =
        sequence.sampleChannel(kCameraRailTrackName, kCameraRailChannelName, timeSeconds, linearFallback);
    // CameraRail::sample()/samplePosition() already clamp their own `t`
    // internally (see CameraRail.cpp's own resolveSegment()), but this
    // function's own contract is "returns a value in [0,1]" regardless
    // of what a hand-authored curve's keys/tangents happen to overshoot
    // to -- explicit here rather than relying on a caller two files away.
    return std::clamp(sampled, 0.0f, 1.0f);
}

PostFxSample postFxAtTime(const Sequence& sequence, float timeSeconds, const PostFxSample& fallback) {
    PostFxSample result;
    result.bloomIntensity =
        sequence.sampleChannel(kPostFxTrackName, kBloomIntensityChannelName, timeSeconds, fallback.bloomIntensity);
    result.bloomThreshold =
        sequence.sampleChannel(kPostFxTrackName, kBloomThresholdChannelName, timeSeconds, fallback.bloomThreshold);
    result.exposure = sequence.sampleChannel(kPostFxTrackName, kExposureChannelName, timeSeconds, fallback.exposure);
    return result;
}

} // namespace engine::cinematic
