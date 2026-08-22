#include "cinematic/TimelineLayout.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace engine::cinematic {

namespace {
constexpr float kMinPixelsPerSecond = 1.0f;
constexpr float kMaxPixelsPerSecond = 20000.0f;
} // namespace

float clampZoom(float pixelsPerSecond) {
    if (!std::isfinite(pixelsPerSecond)) return 100.0f;
    return std::clamp(pixelsPerSecond, kMinPixelsPerSecond, kMaxPixelsPerSecond);
}

float timeToPixel(const TimelineView& view, float timeSeconds) {
    return (timeSeconds - view.scrollSeconds) * clampZoom(view.pixelsPerSecond);
}

float pixelToTime(const TimelineView& view, float pixelX) {
    return view.scrollSeconds + pixelX / clampZoom(view.pixelsPerSecond);
}

float visibleStartSeconds(const TimelineView& view) { return view.scrollSeconds; }

float visibleEndSeconds(const TimelineView& view) {
    return view.scrollSeconds + std::max(view.widthPixels, 0.0f) / clampZoom(view.pixelsPerSecond);
}

bool isTimeVisible(const TimelineView& view, float timeSeconds) {
    return timeSeconds >= visibleStartSeconds(view) && timeSeconds <= visibleEndSeconds(view);
}

size_t hitTestKeyframe(const TimelineView& view, const std::vector<Keyframe>& keys, float pixelX) {
    size_t bestIndex = std::numeric_limits<size_t>::max();
    float bestDistance = view.grabRadiusPixels;

    for (size_t i = 0; i < keys.size(); ++i) {
        const float keyPixel = timeToPixel(view, keys[i].timeSeconds);
        const float distance = std::fabs(keyPixel - pixelX);
        // Strictly-less keeps the FIRST of two exactly-equidistant keys,
        // which is stable across frames -- flipping between them under a
        // stationary cursor would make dragging feel broken.
        if (distance < bestDistance) {
            bestDistance = distance;
            bestIndex = i;
        }
    }
    return bestIndex;
}

float gridIntervalSeconds(const TimelineView& view, float targetPixelSpacing) {
    const float zoom = clampZoom(view.pixelsPerSecond);
    const float rawInterval = std::max(targetPixelSpacing, 1.0f) / zoom;

    // Snap to a 1/2/5 x 10^n sequence. An arbitrary interval produces
    // labels like 0.37s, which nobody can read a timeline against.
    const float exponent = std::floor(std::log10(std::max(rawInterval, 1e-6f)));
    const float magnitude = std::pow(10.0f, exponent);
    const float normalized = rawInterval / magnitude;

    float snapped = 1.0f;
    if (normalized > 5.0f) {
        snapped = 10.0f;
    } else if (normalized > 2.0f) {
        snapped = 5.0f;
    } else if (normalized > 1.0f) {
        snapped = 2.0f;
    }
    return snapped * magnitude;
}

float dragTimeForPixel(const TimelineView& view, float pixelX, SequenceFrameRate rate, bool snapToFrames) {
    float time = std::max(pixelToTime(view, pixelX), 0.0f);
    if (snapToFrames) time = snapToFrame(time, rate);
    return time;
}

int hitTestLoopHandle(const TimelineView& view, float loopStart, float loopEnd, float pixelX) {
    if (loopEnd <= loopStart) return 0; // looping disabled: no handles to grab

    const float startPixel = timeToPixel(view, loopStart);
    const float endPixel = timeToPixel(view, loopEnd);
    const float startDistance = std::fabs(startPixel - pixelX);
    const float endDistance = std::fabs(endPixel - pixelX);

    if (startDistance > view.grabRadiusPixels && endDistance > view.grabRadiusPixels) return 0;
    // When both are in range (a very short loop at low zoom), prefer the
    // nearer one rather than always the start.
    return startDistance <= endDistance ? -1 : 1;
}


int reorderDraggedKeyframe(std::vector<Keyframe>& keys, int index) {
    if (index < 0 || index >= static_cast<int>(keys.size())) return index;
    while (index > 0 && keys[static_cast<size_t>(index) - 1].timeSeconds > keys[static_cast<size_t>(index)].timeSeconds) {
        std::swap(keys[static_cast<size_t>(index) - 1], keys[static_cast<size_t>(index)]);
        --index;
    }
    while (index + 1 < static_cast<int>(keys.size()) &&
           keys[static_cast<size_t>(index) + 1].timeSeconds < keys[static_cast<size_t>(index)].timeSeconds) {
        std::swap(keys[static_cast<size_t>(index) + 1], keys[static_cast<size_t>(index)]);
        ++index;
    }
    return index;
}

} // namespace engine::cinematic
