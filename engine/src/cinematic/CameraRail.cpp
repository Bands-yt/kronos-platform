#include "cinematic/CameraRail.hpp"

#include <algorithm>
#include <cmath>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

#include "cinematic/CurveInterpolation.hpp"

namespace engine::cinematic {

namespace {
constexpr float kEpsilon = 1e-6f;

glm::vec3 safeNormalize(const glm::vec3& v, const glm::vec3& fallback) {
    const float lengthSquared = glm::dot(v, v);
    if (lengthSquared < kEpsilon * kEpsilon) return fallback;
    return v / std::sqrt(lengthSquared);
}

glm::vec3 bezierPoint(const glm::vec3& p0, const glm::vec3& c0, const glm::vec3& c1, const glm::vec3& p1, float t) {
    const float u = 1.0f - t;
    return u * u * u * p0 + 3.0f * u * u * t * c0 + 3.0f * u * t * t * c1 + t * t * t * p1;
}
} // namespace

bool CameraRail::resolveSegment(float t, size_t& outSegment, float& outLocalT) const {
    if (points_.size() < 2) return false;
    const float clamped = std::clamp(t, 0.0f, 1.0f);
    const size_t segmentCount = points_.size() - 1;
    const float scaled = clamped * static_cast<float>(segmentCount);

    // At exactly 1.0 this would index one past the end; clamp to the last
    // segment's end instead.
    size_t segment = static_cast<size_t>(scaled);
    if (segment >= segmentCount) {
        outSegment = segmentCount - 1;
        outLocalT = 1.0f;
        return true;
    }
    outSegment = segment;
    outLocalT = scaled - static_cast<float>(segment);
    return true;
}

glm::vec3 CameraRail::sampleSegment(size_t segmentIndex, float localT) const {
    const RailPoint& from = points_[segmentIndex];
    const RailPoint& to = points_[segmentIndex + 1];

    switch (settings_.splineType) {
        case RailSplineType::Linear:
            return from.position + (to.position - from.position) * localT;
        case RailSplineType::Bezier:
            return bezierPoint(from.position, from.position + from.outTangent, to.position + to.inTangent,
                                to.position, localT);
        case RailSplineType::CatmullRom:
        default: {
            // Catmull-Rom needs a neighbour either side. At the ends the
            // endpoint is duplicated, which makes the curve start and
            // finish cleanly rather than overshooting past the first and
            // last control points.
            const glm::vec3& p0 = segmentIndex > 0 ? points_[segmentIndex - 1].position : from.position;
            const glm::vec3& p3 =
                (segmentIndex + 2) < points_.size() ? points_[segmentIndex + 2].position : to.position;
            return catmullRom(p0, from.position, to.position, p3, localT);
        }
    }
}

glm::vec3 CameraRail::samplePosition(float t) const {
    if (points_.empty()) return glm::vec3(0.0f);
    if (points_.size() == 1) return points_[0].position;

    size_t segment = 0;
    float localT = 0.0f;
    if (!resolveSegment(t, segment, localT)) return points_[0].position;
    return sampleSegment(segment, localT);
}

RailSample CameraRail::sample(float t, float deltaSeconds) {
    RailSample result;
    if (points_.empty()) return result;

    result.position = samplePosition(t);

    // --- lens, interpolated along the rail -------------------------------
    size_t segment = 0;
    float localT = 0.0f;
    if (points_.size() >= 2 && resolveSegment(t, segment, localT)) {
        const RailPoint& from = points_[segment];
        const RailPoint& to = points_[segment + 1];
        result.camera.focalLengthMm = from.focalLengthMm + (to.focalLengthMm - from.focalLengthMm) * localT;
        result.camera.aperture = from.aperture + (to.aperture - from.aperture) * localT;
    } else {
        result.camera.focalLengthMm = points_[0].focalLengthMm;
        result.camera.aperture = points_[0].aperture;
    }

    // --- aim -------------------------------------------------------------
    glm::vec3 desiredAimPoint = settings_.lookAtTarget;
    if (settings_.aimMode == RailAimMode::FollowPath) {
        // Look slightly ahead along the rail. Sampling forward rather
        // than differentiating keeps this correct for every spline type
        // without a per-type derivative.
        const float lookAhead = std::min(t + 0.01f, 1.0f);
        glm::vec3 ahead = samplePosition(lookAhead);
        if (glm::distance(ahead, result.position) < kEpsilon) {
            // At the very end of the rail there is nothing ahead; look
            // back instead and invert, so the camera keeps facing the way
            // it was travelling rather than snapping to a default.
            const glm::vec3 behind = samplePosition(std::max(t - 0.01f, 0.0f));
            ahead = result.position + (result.position - behind);
        }
        desiredAimPoint = ahead;
    }

    // Exponential damping, framed as a time constant so the feel is
    // frame-rate independent -- a fixed per-frame lerp would tighten up
    // at high frame rates and loosen at low ones.
    if (!hasDampedAim_ || settings_.aimDampingSeconds <= kEpsilon || deltaSeconds <= 0.0f) {
        dampedAimPoint_ = desiredAimPoint;
        hasDampedAim_ = true;
    } else {
        const float alpha = 1.0f - std::exp(-deltaSeconds / settings_.aimDampingSeconds);
        dampedAimPoint_ += (desiredAimPoint - dampedAimPoint_) * std::clamp(alpha, 0.0f, 1.0f);
    }

    result.forward = safeNormalize(dampedAimPoint_ - result.position, glm::vec3(0.0f, 0.0f, -1.0f));

    // Guard the degenerate case where the aim direction is parallel to
    // world up: the cross product collapses and the camera would flip.
    glm::vec3 up = settings_.worldUp;
    if (std::fabs(glm::dot(result.forward, safeNormalize(up, glm::vec3(0.0f, 1.0f, 0.0f)))) > 0.999f) {
        up = glm::vec3(0.0f, 0.0f, 1.0f);
    }
    const glm::vec3 right = safeNormalize(glm::cross(result.forward, up), glm::vec3(1.0f, 0.0f, 0.0f));
    result.up = glm::normalize(glm::cross(right, result.forward));

    if (std::fabs(settings_.rollDegrees) > kEpsilon) {
        const glm::quat roll = glm::angleAxis(glm::radians(settings_.rollDegrees), result.forward);
        result.up = roll * result.up;
    }

    result.orientation = glm::quatLookAt(result.forward, result.up);

    // --- focus -----------------------------------------------------------
    if (settings_.autoFocusOnTarget) {
        // Focus on what we are actually aiming at, so a rack focus
        // follows the subject rather than needing to be keyed by hand.
        const float distance = glm::distance(result.position, dampedAimPoint_);
        result.camera.focusDistanceMeters = std::max(distance, 0.05f);
    }

    return result;
}

float CameraRail::approximateLength(int samplesPerSegment) const {
    if (points_.size() < 2) return 0.0f;
    const int steps = std::max(samplesPerSegment, 1) * static_cast<int>(points_.size() - 1);

    float length = 0.0f;
    glm::vec3 previous = samplePosition(0.0f);
    for (int i = 1; i <= steps; ++i) {
        const glm::vec3 current = samplePosition(static_cast<float>(i) / static_cast<float>(steps));
        length += glm::distance(previous, current);
        previous = current;
    }
    return length;
}

float CameraRail::parameterAtDistance(float distanceMeters, int samplesPerSegment) const {
    if (points_.size() < 2) return 0.0f;
    const float total = approximateLength(samplesPerSegment);
    if (total <= kEpsilon) return 0.0f;
    if (distanceMeters <= 0.0f) return 0.0f;
    if (distanceMeters >= total) return 1.0f;

    // Walk the same subdivision approximateLength used, so the two agree.
    const int steps = std::max(samplesPerSegment, 1) * static_cast<int>(points_.size() - 1);
    float travelled = 0.0f;
    glm::vec3 previous = samplePosition(0.0f);
    for (int i = 1; i <= steps; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        const glm::vec3 current = samplePosition(t);
        const float segmentLength = glm::distance(previous, current);
        if (travelled + segmentLength >= distanceMeters) {
            // Interpolate within this step for sub-step accuracy.
            const float remainder = distanceMeters - travelled;
            const float fraction = segmentLength > kEpsilon ? remainder / segmentLength : 0.0f;
            const float previousT = static_cast<float>(i - 1) / static_cast<float>(steps);
            return previousT + (t - previousT) * fraction;
        }
        travelled += segmentLength;
        previous = current;
    }
    return 1.0f;
}

} // namespace engine::cinematic
