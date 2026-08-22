#pragma once

#include <cstddef>
#include <vector>

#include "cinematic/Sequencer.hpp"

namespace engine::cinematic {

// Timeline interaction maths.
//
// Separated from the ImGui panel deliberately: the time-to-pixel mapping,
// hit-testing and snapping are where the real bugs in a timeline live
// (off-by-one on the last frame, keys unclickable at the right edge,
// scrubbing drifting at high zoom), and none of that is testable while it
// is tangled up in draw calls. The panel becomes a thin renderer over
// this.
//
// All positions are in pixels relative to the track area's left edge.

struct TimelineView {
    // Seconds visible at the left edge.
    float scrollSeconds = 0.0f;
    // Horizontal zoom.
    float pixelsPerSecond = 100.0f;
    // Width of the drawable track area, in pixels.
    float widthPixels = 800.0f;
    // How close (in pixels) a click must be to grab a keyframe. Generous
    // on purpose: a 1px hit target is unusable, and keys are small.
    float grabRadiusPixels = 6.0f;
};

[[nodiscard]] float timeToPixel(const TimelineView& view, float timeSeconds);
[[nodiscard]] float pixelToTime(const TimelineView& view, float pixelX);

// Visible time range, useful for culling keys that are off-screen rather
// than drawing thousands of them.
[[nodiscard]] float visibleStartSeconds(const TimelineView& view);
[[nodiscard]] float visibleEndSeconds(const TimelineView& view);
[[nodiscard]] bool isTimeVisible(const TimelineView& view, float timeSeconds);

// Clamps a zoom to something usable. Zero or negative pixels-per-second
// would make every time map to the same pixel and divide by zero on the
// way back.
[[nodiscard]] float clampZoom(float pixelsPerSecond);

// Index of the keyframe nearest `pixelX` within the grab radius, or
// SIZE_MAX when nothing is close enough.
//
// Returns the NEAREST rather than the first match, so overlapping keys at
// high zoom still select the one actually clicked.
[[nodiscard]] size_t hitTestKeyframe(const TimelineView& view, const std::vector<Keyframe>& keys, float pixelX);

// Spacing of gridlines that keeps them readable at any zoom: roughly one
// line per `targetPixelSpacing`, snapped to a sensible time interval
// rather than an arbitrary fraction of a second.
[[nodiscard]] float gridIntervalSeconds(const TimelineView& view, float targetPixelSpacing = 80.0f);

// Where a drag should place a key: converts pixels to time, clamps to
// non-negative, and snaps to the frame grid so authored keys always land
// on frames.
[[nodiscard]] float dragTimeForPixel(const TimelineView& view, float pixelX, SequenceFrameRate rate, bool snapToFrames);

// Restores sort order after a drag moved the key at `index` in time, and
// returns that key's NEW index.
//
// Exists instead of erase-then-insertKeyframe() for two reasons, both of
// which cost a key mid-drag: insertKeyframe() REPLACES any key at the
// same exact time, so dragging one key across another silently destroys
// it; and re-finding the moved key afterwards by comparing times is a
// float equality test that picks the wrong key whenever two share a time.
// Swapping past neighbours keeps every key and tracks the dragged one by
// position, so its identity survives the whole gesture.
[[nodiscard]] int reorderDraggedKeyframe(std::vector<Keyframe>& keys, int index);

// Loop-region handle hit testing. Returns -1 for the start handle, +1 for
// the end handle, 0 for neither.
[[nodiscard]] int hitTestLoopHandle(const TimelineView& view, float loopStart, float loopEnd, float pixelX);

} // namespace engine::cinematic
