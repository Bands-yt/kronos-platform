#include "cinematic/RailCamera.hpp"

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>

#include "core/PhysicalCamera.hpp"

namespace engine::cinematic {

namespace {
constexpr float kEpsilon = 1e-6f;
}

core::Camera cameraFromRailSample(const RailSample& sample, float nearPlane, float farPlane) {
    core::Camera camera;
    camera.position = sample.position;
    camera.nearPlane = nearPlane;
    camera.farPlane = farPlane;
    camera.verticalFovDegrees = core::verticalFovDegrees(sample.camera);

    const glm::vec3 forward = glm::length(sample.forward) > kEpsilon ? glm::normalize(sample.forward)
                                                                      : glm::vec3(0.0f, 0.0f, -1.0f);

    // Exact inverse of core::Camera::forward()'s own formula:
    // forward = (cos(yaw)cos(pitch), sin(pitch), sin(yaw)cos(pitch)).
    camera.pitchDegrees = glm::degrees(std::asin(std::clamp(forward.y, -1.0f, 1.0f)));
    camera.yawDegrees = glm::degrees(std::atan2(forward.z, forward.x));

    // Roll: CameraRail::sample() builds `up` by taking the "roll = 0" up
    // implied by `forward` and world-up, then rotating it by rollDegrees
    // around `forward` (see that function's own comment). Measuring the
    // signed angle between that same "roll = 0" up and the rail's actual
    // `up` recovers exactly the value it applied, rather than a fitted
    // approximation.
    const glm::vec3 worldUp = std::fabs(glm::dot(forward, glm::vec3(0.0f, 1.0f, 0.0f))) > 0.999f
                                   ? glm::vec3(0.0f, 0.0f, 1.0f)
                                   : glm::vec3(0.0f, 1.0f, 0.0f);
    const glm::vec3 right = glm::normalize(glm::cross(forward, worldUp));
    const glm::vec3 unrolledUp = glm::normalize(glm::cross(right, forward));

    if (glm::length(sample.up) > kEpsilon) {
        const glm::vec3 railUp = glm::normalize(sample.up);
        const float cosRoll = std::clamp(glm::dot(unrolledUp, railUp), -1.0f, 1.0f);
        const float sinRoll = glm::dot(glm::cross(unrolledUp, railUp), forward);
        camera.rollDegrees = glm::degrees(std::atan2(sinRoll, cosRoll));
    }

    return camera;
}

} // namespace engine::cinematic
