#include "cinematic/ClipTimeline.hpp"

#include <algorithm>
#include <limits>

namespace engine::cinematic {

namespace {
constexpr float kUnbounded = std::numeric_limits<float>::infinity();

// End of the clip immediately before `clipIndex`, or 0.0f for the first
// clip -- the lower neighbor bound every start-side clamp shares.
float previousClipEnd(const std::vector<MediaClip>& clips, size_t clipIndex) {
    if (clipIndex == 0) return 0.0f;
    const MediaClip& prev = clips[clipIndex - 1];
    return prev.timelineStart + prev.timelineDuration;
}

// Start of the clip immediately after `clipIndex`, or unbounded for the
// last clip -- the upper neighbor bound every end-side clamp shares.
float nextClipStart(const std::vector<MediaClip>& clips, size_t clipIndex) {
    if (clipIndex + 1 >= clips.size()) return kUnbounded;
    return clips[clipIndex + 1].timelineStart;
}
} // namespace

size_t ClipTimeline::addTrack(const std::string& name, ClipTrackKind kind) {
    ClipTrack track;
    track.name = name;
    track.kind = kind;
    tracks_.push_back(std::move(track));
    return tracks_.size() - 1;
}

void ClipTimeline::removeTrack(size_t trackIndex) {
    if (trackIndex >= tracks_.size()) return;
    tracks_.erase(tracks_.begin() + static_cast<long>(trackIndex));
}

size_t ClipTimeline::addClip(size_t trackIndex, const std::string& assetPath, float start, float duration) {
    if (trackIndex >= tracks_.size() || duration < kMinClipDurationSeconds || start < 0.0f) return SIZE_MAX;
    std::vector<MediaClip>& clips = tracks_[trackIndex].clips;

    size_t insertAt = 0;
    while (insertAt < clips.size() && clips[insertAt].timelineStart < start) ++insertAt;

    float lowerBound = previousClipEnd(clips, insertAt);
    float upperBound = insertAt < clips.size() ? clips[insertAt].timelineStart : kUnbounded;
    if (start < lowerBound || start + duration > upperBound) return SIZE_MAX;

    MediaClip clip;
    clip.assetPath = assetPath;
    clip.timelineStart = start;
    clip.timelineDuration = duration;
    clips.insert(clips.begin() + static_cast<long>(insertAt), std::move(clip));
    return insertAt;
}

bool ClipTimeline::splitClip(size_t trackIndex, size_t clipIndex, float timeSeconds) {
    if (trackIndex >= tracks_.size()) return false;
    std::vector<MediaClip>& clips = tracks_[trackIndex].clips;
    if (clipIndex >= clips.size()) return false;

    MediaClip original = clips[clipIndex];
    float end = original.timelineStart + original.timelineDuration;
    if (timeSeconds <= original.timelineStart || timeSeconds >= end) return false;

    MediaClip first = original;
    first.timelineDuration = timeSeconds - original.timelineStart;
    first.fadeOutSeconds = 0.0f;
    first.fadeInSeconds = std::min(first.fadeInSeconds, first.timelineDuration);

    MediaClip second = original;
    second.timelineStart = timeSeconds;
    second.timelineDuration = end - timeSeconds;
    second.sourceOffsetSeconds = original.sourceOffsetSeconds + (timeSeconds - original.timelineStart);
    second.fadeInSeconds = 0.0f;
    second.fadeOutSeconds = std::min(second.fadeOutSeconds, second.timelineDuration);

    clips[clipIndex] = first;
    clips.insert(clips.begin() + static_cast<long>(clipIndex) + 1, second);
    return true;
}

float ClipTimeline::trimClipStart(size_t trackIndex, size_t clipIndex, float newStart) {
    if (trackIndex >= tracks_.size()) return 0.0f;
    std::vector<MediaClip>& clips = tracks_[trackIndex].clips;
    if (clipIndex >= clips.size()) return 0.0f;
    MediaClip& clip = clips[clipIndex];

    float end = clip.timelineStart + clip.timelineDuration;
    float lowerBound = std::max(previousClipEnd(clips, clipIndex), clip.timelineStart - clip.sourceOffsetSeconds);
    float upperBound = end - kMinClipDurationSeconds;
    float clamped = std::clamp(newStart, lowerBound, std::max(lowerBound, upperBound));

    float delta = clamped - clip.timelineStart;
    clip.timelineStart = clamped;
    clip.timelineDuration = end - clamped;
    clip.sourceOffsetSeconds += delta;
    clip.fadeInSeconds = std::min(clip.fadeInSeconds, clip.timelineDuration);
    return clamped;
}

float ClipTimeline::trimClipEnd(size_t trackIndex, size_t clipIndex, float newEnd) {
    if (trackIndex >= tracks_.size()) return 0.0f;
    std::vector<MediaClip>& clips = tracks_[trackIndex].clips;
    if (clipIndex >= clips.size()) return 0.0f;
    MediaClip& clip = clips[clipIndex];

    float lowerBound = clip.timelineStart + kMinClipDurationSeconds;
    float upperBound = nextClipStart(clips, clipIndex);
    float clamped = std::clamp(newEnd, lowerBound, std::max(lowerBound, upperBound));

    clip.timelineDuration = clamped - clip.timelineStart;
    clip.fadeOutSeconds = std::min(clip.fadeOutSeconds, clip.timelineDuration);
    return clamped;
}

float ClipTimeline::moveClip(size_t trackIndex, size_t clipIndex, float newStart) {
    if (trackIndex >= tracks_.size()) return 0.0f;
    std::vector<MediaClip>& clips = tracks_[trackIndex].clips;
    if (clipIndex >= clips.size()) return 0.0f;
    MediaClip& clip = clips[clipIndex];

    float lowerBound = previousClipEnd(clips, clipIndex);
    float upperNeighbor = nextClipStart(clips, clipIndex);
    float upperBound = upperNeighbor == kUnbounded ? kUnbounded : upperNeighbor - clip.timelineDuration;
    float clamped = std::clamp(std::max(newStart, 0.0f), lowerBound, std::max(lowerBound, upperBound));

    clip.timelineStart = clamped;
    return clamped;
}

void ClipTimeline::setFade(size_t trackIndex, size_t clipIndex, float fadeInSeconds, float fadeOutSeconds) {
    if (trackIndex >= tracks_.size()) return;
    std::vector<MediaClip>& clips = tracks_[trackIndex].clips;
    if (clipIndex >= clips.size()) return;
    MediaClip& clip = clips[clipIndex];

    float in = std::clamp(fadeInSeconds, 0.0f, clip.timelineDuration);
    float out = std::clamp(fadeOutSeconds, 0.0f, clip.timelineDuration);
    float total = in + out;
    if (total > clip.timelineDuration && total > 0.0f) {
        float scale = clip.timelineDuration / total;
        in *= scale;
        out *= scale;
    }
    clip.fadeInSeconds = in;
    clip.fadeOutSeconds = out;
}

float ClipTimeline::envelopeValueAtTime(const MediaClip& clip, float timeSeconds) {
    float local = timeSeconds - clip.timelineStart;
    if (local < 0.0f || local >= clip.timelineDuration) return 0.0f;

    float gain = 1.0f;
    if (clip.fadeInSeconds > 0.0f && local < clip.fadeInSeconds) {
        gain = std::min(gain, local / clip.fadeInSeconds);
    }
    float toEnd = clip.timelineDuration - local;
    if (clip.fadeOutSeconds > 0.0f && toEnd < clip.fadeOutSeconds) {
        gain = std::min(gain, toEnd / clip.fadeOutSeconds);
    }
    return std::clamp(gain, 0.0f, 1.0f);
}

float ClipTimeline::durationSeconds() const {
    float longest = 0.0f;
    for (const ClipTrack& track : tracks_) {
        for (const MediaClip& clip : track.clips) {
            longest = std::max(longest, clip.timelineStart + clip.timelineDuration);
        }
    }
    return longest;
}

} // namespace engine::cinematic
