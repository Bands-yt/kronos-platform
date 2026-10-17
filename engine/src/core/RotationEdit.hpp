#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace engine::core {

// Turns an Euler edit (degrees, XYZ) into a rotation without gimbal-lock jumps:
// the new rotation is built from the old quaternion plus the delta of the edited angles.
inline glm::quat applyEulerEdit(const glm::quat& current, const glm::vec3& oldDegrees, const glm::vec3& newDegrees) {
    const glm::vec3 delta = glm::radians(newDegrees - oldDegrees);
    const glm::quat dq = glm::normalize(glm::quat(glm::vec3(delta.x, 0.0f, 0.0f)) * glm::quat(glm::vec3(0.0f, delta.y, 0.0f)) *
                                        glm::quat(glm::vec3(0.0f, 0.0f, delta.z)));
    return glm::normalize(dq * current);
}

// Shortest-path interpolation (flips the sign when the two quaternions are on opposite hemispheres).
inline glm::quat shortestSlerp(const glm::quat& a, glm::quat b, float t) {
    if (glm::dot(a, b) < 0.0f) b = -b;
    return glm::normalize(glm::slerp(a, b, t));
}

} // namespace engine::core

namespace engine::core {

// Rotation that stands an object on a surface: its up axis follows the surface normal,
// and the heading around that axis is kept from `current` as far as possible.
inline glm::quat alignToSurface(const glm::quat& current, const glm::vec3& surfaceNormal) {
    const glm::vec3 n = glm::length(surfaceNormal) > 1e-6f ? glm::normalize(surfaceNormal) : glm::vec3(0, 1, 0);
    const glm::vec3 up = current * glm::vec3(0, 1, 0);
    const float d = glm::clamp(glm::dot(up, n), -1.0f, 1.0f);
    if (d > 0.99999f) return current;
    glm::vec3 axis = glm::cross(up, n);
    if (glm::length(axis) < 1e-6f) axis = current * glm::vec3(1, 0, 0); // opposite: flip around a side axis
    return glm::normalize(glm::angleAxis(std::acos(d), glm::normalize(axis)) * current);
}

} // namespace engine::core
