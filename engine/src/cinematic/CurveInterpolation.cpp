#include "cinematic/CurveInterpolation.hpp"

#include <algorithm>
#include <cmath>

namespace engine::cinematic {

namespace {
constexpr float kEpsilon = 1e-6f;

// Binary search for the last key at or before `timeSeconds`. Linear
// scanning would be fine for a handful of keys and quietly quadratic for
// a real shot with hundreds across dozens of tracks, sampled every frame.
size_t segmentIndexAtTime(const std::vector<Keyframe>& keys, float timeSeconds) {
    size_t low = 0;
    size_t high = keys.size() - 1;
    while (low < high) {
        const size_t mid = low + (high - low + 1) / 2;
        if (keys[mid].timeSeconds <= timeSeconds) {
            low = mid;
        } else {
            high = mid - 1;
        }
    }
    return low;
}
} // namespace

float cubicBezier(float p0, float p1, float p2, float p3, float t) {
    const float u = 1.0f - t;
    return u * u * u * p0 + 3.0f * u * u * t * p1 + 3.0f * u * t * t * p2 + t * t * t * p3;
}

float catmullRom(float p0, float p1, float p2, float p3, float t) {
    const float t2 = t * t;
    const float t3 = t2 * t;
    return 0.5f * ((2.0f * p1) + (-p0 + p2) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
                    (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
}

glm::vec3 catmullRom(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3, float t) {
    return glm::vec3(catmullRom(p0.x, p1.x, p2.x, p3.x, t), catmullRom(p0.y, p1.y, p2.y, p3.y, t),
                      catmullRom(p0.z, p1.z, p2.z, p3.z, t));
}

float bezierValueAtTime(const Keyframe& from, const Keyframe& to, float timeSeconds) {
    const float span = to.timeSeconds - from.timeSeconds;
    if (span <= kEpsilon) return to.value;

    // Control points in (time, value) space.
    const float t0 = from.timeSeconds;
    const float t1 = from.timeSeconds + from.outHandle.x;
    const float t2 = to.timeSeconds + to.inHandle.x;
    const float t3 = to.timeSeconds;

    const float v0 = from.value;
    const float v1 = from.value + from.outHandle.y;
    const float v2 = to.value + to.inHandle.y;
    const float v3 = to.value;

    // Invert the time axis: find the parameter whose time equals the one
    // asked for. Bisection rather than Newton -- the derivative can be
    // zero or negative if an author drags a handle past its neighbour,
    // and bisection stays correct where Newton would diverge.
    float low = 0.0f;
    float high = 1.0f;
    float parameter = std::clamp((timeSeconds - t0) / span, 0.0f, 1.0f); // good initial guess
    for (int iteration = 0; iteration < 24; ++iteration) {
        const float sampledTime = cubicBezier(t0, t1, t2, t3, parameter);
        if (std::fabs(sampledTime - timeSeconds) < 1e-5f) break;
        if (sampledTime < timeSeconds) {
            low = parameter;
        } else {
            high = parameter;
        }
        parameter = 0.5f * (low + high);
    }
    return cubicBezier(v0, v1, v2, v3, parameter);
}

float sampleCurve(const std::vector<Keyframe>& keys, float timeSeconds) {
    if (keys.empty()) return 0.0f;
    if (keys.size() == 1) return keys[0].value;

    // Clamp rather than extrapolate: a cubic run past its data produces
    // values that look like a bug in the shot rather than in the curve.
    if (timeSeconds <= keys.front().timeSeconds) return keys.front().value;
    if (timeSeconds >= keys.back().timeSeconds) return keys.back().value;

    const size_t index = segmentIndexAtTime(keys, timeSeconds);
    if (index + 1 >= keys.size()) return keys.back().value;

    const Keyframe& from = keys[index];
    const Keyframe& to = keys[index + 1];
    const float span = to.timeSeconds - from.timeSeconds;
    // Two keys at the same time: the later one wins rather than dividing
    // by zero.
    if (span <= kEpsilon) return to.value;
    const float t = (timeSeconds - from.timeSeconds) / span;

    switch (from.mode) {
        case InterpolationMode::Stepped:
            return from.value;
        case InterpolationMode::Linear:
            return from.value + (to.value - from.value) * t;
        case InterpolationMode::Bezier:
            return bezierValueAtTime(from, to, timeSeconds);
        case InterpolationMode::Cubic:
        default: {
            // Catmull-Rom needs one key either side; at the ends the
            // segment's own endpoint is reused, which makes the curve
            // start and finish cleanly instead of overshooting.
            const Keyframe& before = index > 0 ? keys[index - 1] : from;
            const Keyframe& after = (index + 2) < keys.size() ? keys[index + 2] : to;
            return catmullRom(before.value, from.value, to.value, after.value, t);
        }
    }
}

void insertKeyframe(std::vector<Keyframe>& keys, const Keyframe& key) {
    const auto position = std::lower_bound(keys.begin(), keys.end(), key.timeSeconds,
                                            [](const Keyframe& candidate, float time) {
                                                return candidate.timeSeconds < time;
                                            });
    // Replace an exact-time match rather than creating a duplicate the
    // sampler would then have to disambiguate.
    if (position != keys.end() && std::fabs(position->timeSeconds - key.timeSeconds) <= kEpsilon) {
        *position = key;
        return;
    }
    keys.insert(position, key);
}

} // namespace engine::cinematic
