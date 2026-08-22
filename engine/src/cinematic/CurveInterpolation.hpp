#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

namespace engine::cinematic {

// Keyframe interpolation for the sequencer.
//
// Deliberately free of ImGui, Vulkan and ECS: this is the maths every
// track type shares, and keeping it separable is what makes it testable
// without standing up an editor.
//
// Time is in seconds throughout. Keyframes are kept sorted by time; the
// sampling functions assume that and say so rather than re-sorting on
// every evaluation, which would be a per-frame allocation and sort.

enum class InterpolationMode : uint8_t {
    // Holds the previous value until the next key, then jumps. Correct
    // for things that genuinely switch rather than blend -- a light
    // turning on, a script trigger firing.
    Stepped = 0,
    Linear = 1,
    // Catmull-Rom through the neighbouring keys. Smooth without asking
    // the author to place handles, which is what most tracks want.
    Cubic = 2,
    // Author-controlled cubic with explicit in/out handles, for when the
    // easing itself is the point.
    Bezier = 3,
};

struct Keyframe {
    float timeSeconds = 0.0f;
    float value = 0.0f;
    InterpolationMode mode = InterpolationMode::Cubic;
    // Bezier handles, expressed as offsets from this key in (time, value)
    // space. `inHandle` shapes the segment arriving at this key,
    // `outHandle` the segment leaving it.
    glm::vec2 inHandle{-0.1f, 0.0f};
    glm::vec2 outHandle{0.1f, 0.0f};
};

// Samples a sorted keyframe track at `timeSeconds`.
//
// Before the first key returns the first value, after the last returns
// the last -- clamping rather than extrapolating, because extrapolating a
// cubic past its data produces wild values that look like a bug in the
// shot rather than in the curve.
//
// The interpolation used for a segment is the mode of the key at its
// START, so an author changes a segment by editing the key it leaves.
[[nodiscard]] float sampleCurve(const std::vector<Keyframe>& keys, float timeSeconds);

// Cubic Bezier with the standard basis, on the value axis only.
[[nodiscard]] float cubicBezier(float p0, float p1, float p2, float p3, float t);

// Catmull-Rom through p1 and p2, shaped by neighbours p0 and p3.
[[nodiscard]] float catmullRom(float p0, float p1, float p2, float p3, float t);
[[nodiscard]] glm::vec3 catmullRom(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3,
                                    float t);

// Solves a Bezier segment for the value at a given TIME.
//
// A Bezier keyframe's handles move in time as well as value, so the
// curve is parametric: the parameter t is not the same as normalised
// time. This inverts it numerically. Without this step, dragging a handle
// sideways would change the timing of everything after it.
[[nodiscard]] float bezierValueAtTime(const Keyframe& from, const Keyframe& to, float timeSeconds);

// Inserts while keeping the track sorted, and replaces any key already at
// that exact time rather than creating a duplicate the sampler would
// then have to disambiguate.
void insertKeyframe(std::vector<Keyframe>& keys, const Keyframe& key);

} // namespace engine::cinematic
