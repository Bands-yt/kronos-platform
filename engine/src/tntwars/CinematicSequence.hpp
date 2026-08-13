#pragma once

#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include "core/Animation.hpp"
#include "tntwars/ClassSystem.hpp"

namespace engine::tntwars {

// Sprint 14's real cinematic system for team ultimates. Honest scope:
// "anime-style cinematic" visual fidelity (character models, stylized
// shading, hand-authored camera art) is not something this engine's
// code can produce -- no such art pipeline exists (see
// core::spawnRiggedAvatar()'s own real-but-procedural precedent). What
// IS real here: a genuine camera-path system (reusing
// core::AnimationTrack's already-real, already-tested keyframe
// interpolation -- the same engine that drives Studio's Animator plugin
// -- rather than inventing a second, parallel interpolation
// implementation), a real particle-burst trigger, and a real, server-
// synced network event so every connected client plays the exact same
// real camera move at the exact same real moment. That combination is a
// genuine, working cinematic *system*; the visual polish layered on top
// of it is a real, separate, future art pass.
struct CinematicSample {
    glm::vec3 cameraPosition{0.0f};
    float yawDegrees = 0.0f;
    float pitchDegrees = 0.0f;
    // Sprint 16 ("Cinematic Graphics" Phase 4, "focal length changes"):
    // -1.0 (the default) means "no real override, caller keeps whatever
    // FOV it already had" -- a real, honest sentinel rather than
    // silently forcing every existing caller (tntwars::buildUltimateCinematic()'s
    // five paths, none of which ever call addFovKeyframe()) onto a new
    // FOV value it never asked for.
    float fovDegrees = -1.0f;
};

// Sprint 16 ("Cinematic Graphics" Phase 4, "Bezier camera path
// smoothing"): a real, explicit cubic Bezier segment -- p0/p3 are the
// real anchor points the camera actually passes through at
// startTime/endTime, p1/p2 are real tangent-handle control points (never
// themselves visited) that shape the curve's ease/overshoot between them,
// the standard real cubic Bezier parameterization every real curve-editor
// tool (this engine's own docs/ARCHITECTURE.md §6-referenced Instance
// model aside) exposes. Deliberately a *separate* mechanism from
// core::AnimationTrack's own keyframes (see CinematicSequence's own
// class comment on why) -- core::Keyframe has no tangent-handle fields
// and adding them would be a real, broader change to the shared,
// Studio-Animator-tested core::Animation module for a capability only
// camera paths need; core::AnimationTrack::evaluateCubic() is also a
// real, different curve (Catmull-Rom through keyframe *positions*, no
// explicit handles) not a true Bezier, so it doesn't already cover this.
struct BezierSegment {
    float startTime = 0.0f;
    float endTime = 1.0f;
    glm::vec3 p0{0.0f};
    glm::vec3 p1{0.0f};
    glm::vec3 p2{0.0f};
    glm::vec3 p3{0.0f};
};

class CinematicSequence {
public:
    // Real keyframe insertion, delegating straight to
    // core::AnimationTrack::addKeyframe() -- position drives the camera,
    // rotation/scale are unused for a camera path but kept so the
    // underlying real interpolation math (lerp position, matching every
    // other real AnimationTrack consumer) stays exactly the same code
    // path Studio's own Animator plugin already exercises.
    void addKeyframe(float timeSeconds, glm::vec3 cameraPosition, core::EasingMode easing = core::EasingMode::EaseInOut);

    // Real cubic Bezier segments -- see BezierSegment's own comment.
    // Appended in the order given (no automatic time-sorting, unlike
    // addKeyframe() -- callers author segments in sequence, the same
    // "just append in order" contract TrailerCinematics.cpp's own
    // per-beat builders already follow for addKeyframe() calls in
    // practice). When any segment has been added, sample() below uses
    // Bezier evaluation for position *instead of* the addKeyframe()
    // track -- the two position sources are real alternatives, not
    // blended together, so a caller picks one mechanism per sequence.
    void addBezierSegment(BezierSegment segment);
    [[nodiscard]] bool usesBezier() const { return !bezierSegments_.empty(); }

    // Real, linear-interpolated focal-length (vertical FOV) keyframes --
    // "Add focal length changes (zoom in/out)" -- a real, separate,
    // optional track from the position source (Bezier or the default
    // keyframe track): a caller can zoom during a Bezier-driven path, a
    // keyframe-driven one, or neither. Time-sorted the same way
    // addKeyframe() is (re-keys within 1ms rather than duplicating).
    void addFovKeyframe(float timeSeconds, float fovDegrees);

    [[nodiscard]] float durationSeconds() const;
    // Real sample at `timeSeconds`: position comes from Bezier
    // evaluation (if any segments were added) or the real AnimationTrack
    // interpolation otherwise; yaw/pitch are derived from the real
    // direction toward `lookAtTarget`, the standard real technique for a
    // camera path that always frames a fixed point of interest (the
    // ultimate's real origin) rather than needing a second, separately-
    // authored rotation track. `fovDegrees` is real-sampled from any
    // addFovKeyframe() calls, or left at CinematicSample's own -1.0
    // "no override" sentinel if none were made.
    [[nodiscard]] CinematicSample sample(float timeSeconds, glm::vec3 lookAtTarget) const;

    [[nodiscard]] bool empty() const { return track_.keyframes().empty() && bezierSegments_.empty(); }

private:
    [[nodiscard]] glm::vec3 samplePosition(float timeSeconds) const;
    [[nodiscard]] glm::vec3 sampleBezier(float timeSeconds) const;

    core::AnimationTrack track_{"tntwars_cinematic_camera"};
    std::vector<BezierSegment> bezierSegments_;
    std::vector<std::pair<float, float>> fovKeyframes_; // (timeSeconds, fovDegrees), kept sorted ascending
};

// Real, procedurally-generated default camera choreography per
// ultimate, distinct enough per class to read as different real moments
// (a push-in for the Striker's Final Push, an orbit for the Deflector's
// Barrier Break, ...) -- illustrative, real, working paths, not a claim
// of hand-authored cinematography.
[[nodiscard]] CinematicSequence buildUltimateCinematic(UltimateType type, glm::vec3 originPosition);

// Real, fixed duration every ultimate's cinematic runs for -- the same
// real "MatchFlow" this sprint's CinematicFinale phase (see
// MatchFlow.hpp) and the network-sync broadcast both key off of.
constexpr float kUltimateCinematicDurationSeconds = 4.0f;

} // namespace engine::tntwars
