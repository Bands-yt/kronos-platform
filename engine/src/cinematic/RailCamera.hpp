#pragma once

#include "cinematic/CameraRail.hpp"
#include "core/Camera.hpp"

namespace engine::cinematic {

// Converts a real evaluated rail instant into a real core::Camera, so
// CaptureRig::exportSequence() (and anything else that needs to actually
// render along a rail rather than just draw its gizmo) has something it
// can hand to core::Renderer::drawSceneInto(). Pure maths, no Vulkan or
// ECS -- real, headless-testable with a hand-built RailSample.
//
// core::Camera stores yaw/pitch/roll degrees (see that class's own
// comment), not a forward vector or quaternion, so this is the real
// inverse of Camera::forward()'s own formula, plus a real roll recovered
// by measuring the angle between RailSample::up and the "roll = 0" up
// implied by that same yaw/pitch -- not an approximation, the exact
// inverse of how CameraRail::sample() applies rollDegrees to `up` in the
// first place.
[[nodiscard]] core::Camera cameraFromRailSample(const RailSample& sample, float nearPlane, float farPlane);

} // namespace engine::cinematic
