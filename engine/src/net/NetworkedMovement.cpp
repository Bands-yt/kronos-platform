#include "net/NetworkedMovement.hpp"

#include <algorithm>
#include <cmath>

#include <glm/gtc/quaternion.hpp>

namespace engine::net {

namespace {
// Matches CharacterController::Settings::jumpSpeed/capsule geometry, so
// the networked avatar's jump feels the same as the offline one.
constexpr float kJumpSpeed = 7.0f;
constexpr float kCapsuleRadius = 0.35f;
constexpr float kCapsuleFootOffset = 0.9f; // capsuleRadius 0.35 + capsuleHalfHeight 0.55
constexpr float kGroundProbeUp = 2.0f;
constexpr float kGroundProbeDistance = 500.0f;
} // namespace

void applyNetworkedMovement(core::Transform& transform, NetworkedVerticalMotion& vertical, const core::Physics& physics,
                             const InputCommand& command, float moveSpeed) {
    float yawRad = glm::radians(command.yaw);
    glm::vec3 forward(std::cos(yawRad), 0.0f, std::sin(yawRad));
    // cross(forward, up), matching CharacterController.cpp's camRight --
    // the old (sin, 0, -cos) was cross(up, forward), the wrong-handed one
    // that made MoveRight/MoveLeft feel swapped.
    glm::vec3 right(-std::sin(yawRad), 0.0f, std::cos(yawRad));

    glm::vec3 worldMove = forward * command.moveAxis.z + right * command.moveAxis.x;
    glm::vec3 horizontalDelta(worldMove.x * moveSpeed * command.deltaTime, 0.0f, worldMove.z * moveSpeed * command.deltaTime);
    float horizontalDist = glm::length(horizontalDelta);
    if (horizontalDist > 0.0001f) {
        // Real, single-ray horizontal blocker check -- transform.position.y
        // is already capsule-center height (see kCapsuleFootOffset), so a
        // flat ray here can't false-hit the ground plane below. Same
        // "one ray, not a full capsule sweep" honesty as
        // CharacterController.cpp's own tryStepUp().
        glm::vec3 moveDir = horizontalDelta / horizontalDist;
        core::Physics::RaycastHit blocker = physics.raycast(transform.position, moveDir, horizontalDist + kCapsuleRadius);
        if (blocker.hit) {
            float allowedDist = std::max(0.0f, blocker.distance - kCapsuleRadius);
            horizontalDelta = moveDir * std::min(allowedDist, horizontalDist);
        }
    }
    transform.position.x += horizontalDelta.x;
    transform.position.z += horizontalDelta.z;

    // Real gravity + ground raycast, same shape as CharacterController's
    // physics-backed jump/fall (just driven by a raycast each tick
    // instead of a live Jolt body) -- so the character actually falls
    // off platform edges instead of sliding along a fixed height, and
    // jump is a real impulse-then-gravity arc, not a "hold to levitate"
    // hover.
    if (command.jump && vertical.grounded) {
        vertical.velocityY = kJumpSpeed;
    }
    vertical.velocityY += physics.gravity().y * command.deltaTime;
    transform.position.y += vertical.velocityY * command.deltaTime;

    core::Physics::RaycastHit ground = physics.raycast(
        transform.position + glm::vec3(0.0f, kGroundProbeUp, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f), kGroundProbeDistance);
    float groundY = ground.point.y + kCapsuleFootOffset;
    vertical.grounded = ground.hit && transform.position.y <= groundY && vertical.velocityY <= 0.0f;
    if (vertical.grounded) {
        transform.position.y = groundY;
        vertical.velocityY = 0.0f;
    }

    // Face actual movement direction, same atan2(x, z) convention
    // CharacterController.cpp's facingYawRadians_ uses -- not
    // command.yaw directly, which is off by a fixed 90 degrees from this
    // one for straight-forward movement. Holds last facing when not
    // moving.
    glm::vec2 horizontalMove(worldMove.x, worldMove.z);
    if (glm::length(horizontalMove) > 0.0001f) {
        transform.rotation = glm::angleAxis(std::atan2(horizontalMove.x, horizontalMove.y), glm::vec3(0.0f, 1.0f, 0.0f));
    }
}

} // namespace engine::net
