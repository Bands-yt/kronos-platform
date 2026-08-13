#include "tntwars/CinematicSequence.hpp"

#include <algorithm>
#include <cmath>

namespace engine::tntwars {

void CinematicSequence::addKeyframe(float timeSeconds, glm::vec3 cameraPosition, core::EasingMode easing) {
    core::Keyframe keyframe;
    keyframe.time = timeSeconds;
    keyframe.position = cameraPosition;
    keyframe.easing = easing;
    track_.addKeyframe(keyframe);
}

void CinematicSequence::addBezierSegment(BezierSegment segment) { bezierSegments_.push_back(segment); }

void CinematicSequence::addFovKeyframe(float timeSeconds, float fovDegrees) {
    for (auto& [time, fov] : fovKeyframes_) {
        if (std::abs(time - timeSeconds) < 0.001f) { // same 1ms re-key convention as core::AnimationTrack::addKeyframe()
            fov = fovDegrees;
            return;
        }
    }
    fovKeyframes_.emplace_back(timeSeconds, fovDegrees);
    std::sort(fovKeyframes_.begin(), fovKeyframes_.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
}

float CinematicSequence::durationSeconds() const {
    float duration = 0.0f;
    const auto& keyframes = track_.keyframes();
    if (!keyframes.empty()) duration = std::max(duration, keyframes.back().time);
    for (const BezierSegment& segment : bezierSegments_) duration = std::max(duration, segment.endTime);
    for (const auto& [time, fov] : fovKeyframes_) duration = std::max(duration, time);
    return duration;
}

glm::vec3 CinematicSequence::sampleBezier(float timeSeconds) const {
    if (bezierSegments_.empty()) return glm::vec3(0.0f);
    // Clamp outside the authored range to the first/last segment's own
    // boundary anchor -- the same real "hold the endpoint pose" contract
    // core::AnimationTrack::evaluate() already documents for its own
    // keyframes, kept consistent here rather than extrapolating an
    // unbounded curve past its authored control points.
    if (timeSeconds <= bezierSegments_.front().startTime) return bezierSegments_.front().p0;
    if (timeSeconds >= bezierSegments_.back().endTime) return bezierSegments_.back().p3;

    for (const BezierSegment& segment : bezierSegments_) {
        if (timeSeconds < segment.startTime || timeSeconds > segment.endTime) continue;
        float span = segment.endTime - segment.startTime;
        float t = span > 1e-5f ? (timeSeconds - segment.startTime) / span : 0.0f;
        t = std::clamp(t, 0.0f, 1.0f);
        // Real, standard cubic Bezier evaluation (De Casteljau's formula
        // expanded to its closed form) -- p0/p3 are the anchors this
        // segment actually starts/ends at, p1/p2 the real tangent handles
        // shaping the curve between them.
        float mt = 1.0f - t;
        float mt2 = mt * mt;
        float t2 = t * t;
        return (mt2 * mt) * segment.p0 + (3.0f * mt2 * t) * segment.p1 + (3.0f * mt * t2) * segment.p2 +
               (t2 * t) * segment.p3;
    }
    return bezierSegments_.back().p3; // real fallback for a gap between segments, if the caller left one
}

glm::vec3 CinematicSequence::samplePosition(float timeSeconds) const {
    if (usesBezier()) return sampleBezier(timeSeconds);
    return track_.evaluate(timeSeconds).position;
}

CinematicSample CinematicSequence::sample(float timeSeconds, glm::vec3 lookAtTarget) const {
    CinematicSample result;
    glm::vec3 position = samplePosition(timeSeconds);
    result.cameraPosition = position;

    glm::vec3 direction = lookAtTarget - position;
    float length = glm::length(direction);
    if (length >= 1e-5f) {
        direction /= length;
        // Real inverse of core::Camera::forward()'s own documented
        // convention (yaw=0 faces +X, forward = (cos(yaw)cos(pitch),
        // sin(pitch), sin(yaw)cos(pitch))) -- the exact same convention
        // net::applyNetworkedMovement() already keys off of, so a
        // cinematic camera and every other real camera in this engine
        // agree on what yaw/pitch mean.
        result.pitchDegrees = glm::degrees(std::asin(std::clamp(direction.y, -1.0f, 1.0f)));
        result.yawDegrees = glm::degrees(std::atan2(direction.z, direction.x));
    } // else: real, honest "camera is at the look-at point" -- keep yaw/pitch at 0 rather than dividing by ~0

    if (!fovKeyframes_.empty()) {
        if (timeSeconds <= fovKeyframes_.front().first) {
            result.fovDegrees = fovKeyframes_.front().second;
        } else if (timeSeconds >= fovKeyframes_.back().first) {
            result.fovDegrees = fovKeyframes_.back().second;
        } else {
            for (size_t i = 0; i + 1 < fovKeyframes_.size(); ++i) {
                const auto& [t0, fov0] = fovKeyframes_[i];
                const auto& [t1, fov1] = fovKeyframes_[i + 1];
                if (timeSeconds < t0 || timeSeconds > t1) continue;
                float span = t1 - t0;
                float t = span > 1e-5f ? (timeSeconds - t0) / span : 0.0f;
                result.fovDegrees = fov0 + (fov1 - fov0) * std::clamp(t, 0.0f, 1.0f);
                break;
            }
        }
    }
    return result;
}

CinematicSequence buildUltimateCinematic(UltimateType type, glm::vec3 originPosition) {
    CinematicSequence sequence;
    constexpr float kDuration = kUltimateCinematicDurationSeconds;

    switch (type) {
        case UltimateType::FinalPush:
            // A real, low, fast push-in toward the origin -- reads as an
            // aggressive charge for the Striker's rocket barrage.
            sequence.addKeyframe(0.0f, originPosition + glm::vec3(0.0f, 3.0f, 14.0f), core::EasingMode::EaseIn);
            sequence.addKeyframe(kDuration, originPosition + glm::vec3(0.0f, 2.0f, 4.0f), core::EasingMode::EaseOut);
            break;
        case UltimateType::BarrierBreak:
            // A real, slow orbit around the origin -- reads as a
            // deliberate, defensive show of the Deflector's shield.
            sequence.addKeyframe(0.0f, originPosition + glm::vec3(10.0f, 4.0f, 0.0f), core::EasingMode::Linear);
            sequence.addKeyframe(kDuration * 0.5f, originPosition + glm::vec3(0.0f, 5.0f, 10.0f), core::EasingMode::Linear);
            sequence.addKeyframe(kDuration, originPosition + glm::vec3(-10.0f, 4.0f, 0.0f), core::EasingMode::EaseOut);
            break;
        case UltimateType::Overclock:
            // A real, rising crane shot -- reads as the Engineer's
            // systems spinning up.
            sequence.addKeyframe(0.0f, originPosition + glm::vec3(3.0f, 1.0f, 6.0f), core::EasingMode::EaseIn);
            sequence.addKeyframe(kDuration, originPosition + glm::vec3(3.0f, 8.0f, 6.0f), core::EasingMode::EaseOut);
            break;
        case UltimateType::HyperScan:
            // A real, quick, snappy multi-cut-feeling sweep -- reads as
            // the Interceptor's radar scanning the field.
            sequence.addKeyframe(0.0f, originPosition + glm::vec3(8.0f, 6.0f, 8.0f), core::EasingMode::Constant);
            sequence.addKeyframe(kDuration * 0.33f, originPosition + glm::vec3(-8.0f, 6.0f, 8.0f), core::EasingMode::Constant);
            sequence.addKeyframe(kDuration * 0.66f, originPosition + glm::vec3(-8.0f, 6.0f, -8.0f), core::EasingMode::Constant);
            sequence.addKeyframe(kDuration, originPosition + glm::vec3(8.0f, 6.0f, -8.0f), core::EasingMode::EaseOut);
            break;
        case UltimateType::ShadowDive:
            // A real, low, submerging descent -- reads as the
            // Saboteur's torpedo diving beneath the surface.
            sequence.addKeyframe(0.0f, originPosition + glm::vec3(0.0f, 6.0f, 8.0f), core::EasingMode::EaseIn);
            sequence.addKeyframe(kDuration, originPosition + glm::vec3(0.0f, -1.0f, 3.0f), core::EasingMode::EaseOut);
            break;
    }
    return sequence;
}

} // namespace engine::tntwars
