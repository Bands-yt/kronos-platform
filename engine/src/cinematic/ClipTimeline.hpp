#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace engine::cinematic {

// A clip-based (CapCut/Premiere-style) timeline: media/3D tracks holding
// ordered, non-overlapping clips with in/out trim and fade envelopes.
//
// Deliberately separate from Sequence (Sequencer.hpp): that class models
// continuous keyframe curves driving a camera/light/transform, where a
// clip here is a discrete span of footage with its own trim-in point
// into a source asset. Conflating the two would mean every curve-track
// operation (insertKeyframe, curve tangents) also has to reason about
// clip adjacency, and vice versa. Both timelines share one playhead
// (see studio::plugins::NleTimelinePlugin) and reuse the same
// TimelineLayout pixel math -- nothing about time-to-pixel mapping is
// specific to keyframes.
//
// Headless on purpose (see TimelineLayout.hpp's own header for why): a
// clip drag's start/end clamping, a razor split's source-offset
// bookkeeping, and a fade envelope are all pure arithmetic with real
// off-by-one edges, and none of it needs ImGui running to be tested.
enum class ClipTrackKind : uint8_t { Media = 0, ThreeD = 1 };

struct MediaClip {
    std::string assetPath;
    float timelineStart = 0.0f;
    float timelineDuration = 1.0f;
    // Offset into the source asset's own timeline that timelineStart
    // corresponds to -- the trim-in point. 0 for a fresh, untrimmed
    // clip; shifts when trimClipStart()/splitClip() move where playback
    // begins without moving *what* plays there.
    float sourceOffsetSeconds = 0.0f;
    float fadeInSeconds = 0.0f;
    float fadeOutSeconds = 0.0f;
};

struct ClipTrack {
    std::string name;
    ClipTrackKind kind = ClipTrackKind::Media;
    bool muted = false;
    std::vector<MediaClip> clips; // kept sorted by timelineStart
};

// Below this, a clip is treated as unusable rather than a sliver a drag
// could shrink to nothing -- matches TimelineLayout::clampZoom()'s own
// "clamp to something usable, don't let arithmetic divide by zero" spirit.
inline constexpr float kMinClipDurationSeconds = 0.05f;

class ClipTimeline {
public:
    [[nodiscard]] size_t addTrack(const std::string& name, ClipTrackKind kind);
    void removeTrack(size_t trackIndex);
    [[nodiscard]] const std::vector<ClipTrack>& tracks() const { return tracks_; }
    [[nodiscard]] std::vector<ClipTrack>& mutableTracks() { return tracks_; }

    // Inserts at [start, start+duration). Returns SIZE_MAX, with no
    // change, if that span overlaps an existing clip on the track --
    // callers doing a drag-drop placement should clamp `start` against
    // neighbors themselves first (see NleTimelinePlugin) if they want a
    // guaranteed placement rather than a rejected one.
    [[nodiscard]] size_t addClip(size_t trackIndex, const std::string& assetPath, float start, float duration);

    // Cuts the clip in two at `timeSeconds`, which must fall strictly
    // inside it (not on either edge -- that would produce a zero-length
    // clip). The new second clip's sourceOffsetSeconds continues where
    // the first leaves off, so the same footage plays across the cut
    // with no jump. The cut edge itself carries no fade (a razor split
    // is a hard cut); each half keeps whichever of the original
    // fadeIn/fadeOut still borders the timeline's outer edge.
    // Returns false, unchanged, if the split point isn't strictly interior.
    bool splitClip(size_t trackIndex, size_t clipIndex, float timeSeconds);

    // Drags the clip's start edge. Clamped to the previous clip's end
    // (or 0), to leaving at least kMinClipDurationSeconds, and to never
    // trimming past the source's own start (sourceOffsetSeconds can't go
    // negative). Returns the start actually applied after clamping.
    float trimClipStart(size_t trackIndex, size_t clipIndex, float newStart);

    // Drags the clip's end edge. Clamped to the next clip's start (or
    // unbounded on the last clip) and the same minimum duration. Returns
    // the end actually applied.
    float trimClipEnd(size_t trackIndex, size_t clipIndex, float newEnd);

    // Slides the whole clip without touching its duration or trim.
    // Clamped so it cannot overlap either neighbor. Returns the start
    // actually applied.
    float moveClip(size_t trackIndex, size_t clipIndex, float newStart);

    // Clamps each to [0, duration], and the pair to fadeIn+fadeOut <=
    // duration -- scaled down proportionally rather than letting one
    // side silently win, so a drag that overshoots both handles at once
    // still leaves a sensible, still-summing-to-the-clip envelope.
    void setFade(size_t trackIndex, size_t clipIndex, float fadeInSeconds, float fadeOutSeconds);

    // [0,1] gain at `timeSeconds`: ramps across fadeIn, full through the
    // middle, ramps down across fadeOut, 0 outside [timelineStart,
    // timelineStart+timelineDuration).
    [[nodiscard]] static float envelopeValueAtTime(const MediaClip& clip, float timeSeconds);

    // Longest clip end across every track.
    [[nodiscard]] float durationSeconds() const;

private:
    std::vector<ClipTrack> tracks_;
};

} // namespace engine::cinematic
