#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "core/PhysicalCamera.hpp"

namespace engine::cinematic {

// Spline-based camera rails.
//
// A rail is a path the camera travels along, plus what it looks at and
// how its lens behaves while it does. Kept free of ECS and Vulkan so the
// motion can be tested without a scene.

enum class RailSplineType : uint8_t {
    // Passes exactly through every control point. What a director expects
    // when they place a point: the camera goes *there*.
    CatmullRom = 0,
    // Control points shape the curve without lying on it. Fewer points
    // for a long sweeping move, at the cost of being less literal.
    Bezier = 1,
    Linear = 2,
};

struct RailPoint {
    glm::vec3 position{0.0f};
    // Bezier control offsets, relative to this point. Ignored for the
    // other spline types.
    glm::vec3 inTangent{-1.0f, 0.0f, 0.0f};
    glm::vec3 outTangent{1.0f, 0.0f, 0.0f};
    // Per-point lens state, interpolated along the rail so a move can
    // rack focus or zoom as it travels.
    float focalLengthMm = 35.0f;
    float aperture = 2.8f;
};

// How the camera is oriented while travelling.
enum class RailAimMode : uint8_t {
    // Face along the direction of travel.
    FollowPath = 0,
    // Face a fixed world point.
    LookAtPoint = 1,
    // Face a point that moves (an actor). The caller updates
    // `lookAtTarget` each frame.
    LookAtTarget = 2,
};

struct CameraRailSettings {
    RailSplineType splineType = RailSplineType::CatmullRom;
    RailAimMode aimMode = RailAimMode::FollowPath;
    glm::vec3 lookAtTarget{0.0f};
    // Seconds for the aim to catch up to its target, roughly. 0 disables
    // damping and the camera snaps.
    //
    // Damping is what separates a camera move that looks operated from
    // one that looks like a robot: a real operator lags a fast subject
    // slightly rather than tracking it perfectly.
    float aimDampingSeconds = 0.25f;
    // Roll about the view axis, degrees.
    float rollDegrees = 0.0f;
    // Keeps the focus plane on whatever is being aimed at, so a rack
    // focus follows the subject instead of being keyed by hand.
    bool autoFocusOnTarget = true;
    glm::vec3 worldUp{0.0f, 1.0f, 0.0f};
};

// One evaluated instant on the rail.
struct RailSample {
    glm::vec3 position{0.0f};
    glm::vec3 forward{0.0f, 0.0f, -1.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};
    glm::quat orientation{1.0f, 0.0f, 0.0f, 0.0f};
    core::PhysicalCamera camera;
};

class CameraRail {
public:
    void addPoint(const RailPoint& point) { points_.push_back(point); }
    void clear() { points_.clear(); }
    [[nodiscard]] const std::vector<RailPoint>& points() const { return points_; }
    [[nodiscard]] size_t pointCount() const { return points_.size(); }

    void setSettings(const CameraRailSettings& settings) { settings_ = settings; }
    [[nodiscard]] const CameraRailSettings& settings() const { return settings_; }
    [[nodiscard]] CameraRailSettings& mutableSettings() { return settings_; }

    // Position along the rail for a normalised parameter in [0,1].
    // Clamped, not wrapped -- a shot running past the end of its rail
    // should stop at the end, not teleport to the start.
    [[nodiscard]] glm::vec3 samplePosition(float t) const;

    // Full evaluation including aim, roll and lens.
    //
    // `deltaSeconds` drives aim damping and therefore makes this
    // STATEFUL: calling it advances the damped aim. Pass 0 for a
    // scrub/preview that must not disturb playback state.
    [[nodiscard]] RailSample sample(float t, float deltaSeconds);

    // Resets damping so the next sample snaps straight to its target.
    // Needed when jumping the playhead: otherwise the camera visibly
    // swings from wherever it was aiming before the cut.
    void resetDamping() { hasDampedAim_ = false; }

    // Approximate arc length, by sampling. Used to travel at constant
    // speed rather than constant parameter -- on a spline those differ,
    // and the difference reads on screen as the camera speeding up
    // through tight curves.
    [[nodiscard]] float approximateLength(int samplesPerSegment = 16) const;

    // Maps a distance along the rail to the parameter that reaches it.
    [[nodiscard]] float parameterAtDistance(float distanceMeters, int samplesPerSegment = 16) const;

private:
    [[nodiscard]] glm::vec3 sampleSegment(size_t segmentIndex, float localT) const;
    [[nodiscard]] bool resolveSegment(float t, size_t& outSegment, float& outLocalT) const;

    std::vector<RailPoint> points_;
    CameraRailSettings settings_{};

    glm::vec3 dampedAimPoint_{0.0f};
    bool hasDampedAim_ = false;
};

} // namespace engine::cinematic
