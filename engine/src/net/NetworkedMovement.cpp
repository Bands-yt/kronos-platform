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
                             const InputCommand& command, float moveSpeed, NetworkedMovementPush* outPush) {
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
        // Real 3-ray fan (center + both capsule-radius edges along the
        // perpendicular axis), most-restrictive-wins -- transform.position.y
        // is already capsule-center height (see kCapsuleFootOffset), so a
        // flat ray here can't false-hit the ground plane below. A single
        // center ray lets the capsule clip through a wall corner/edge that
        // never crosses the exact center line (real, walk-through-the-wall
        // bug at grazing angles); this is the same "more than one raycast,
        // take the most restrictive result" idea CharacterController.cpp's
        // own tryStepUp() already relies on for its low/high probe, applied
        // here across the capsule's own width instead of its height.
        glm::vec3 moveDir = horizontalDelta / horizontalDist;
        glm::vec3 perp(-moveDir.z, 0.0f, moveDir.x);
        float allowedDist = horizontalDist;
        glm::vec3 blockNormal(0.0f);
        core::EntityId blockEntity = core::kNullEntity;
        bool blocked = false;
        for (float side : {0.0f, 1.0f, -1.0f}) {
            glm::vec3 origin = transform.position + perp * (side * kCapsuleRadius * 0.999f);
            core::Physics::RaycastHit blocker = physics.raycast(origin, moveDir, horizontalDist + kCapsuleRadius);
            if (blocker.hit) {
                float candidate = std::max(0.0f, blocker.distance - kCapsuleRadius);
                if (candidate < allowedDist) {
                    allowedDist = candidate;
                    blockNormal = blocker.normal;
                    blockEntity = blocker.entity;
                    blocked = true;
                }
            }
        }
        glm::vec3 primaryDelta = moveDir * std::min(allowedDist, horizontalDist);
        horizontalDelta = primaryDelta;

        // Real "what got pushed into" out-param -- see NetworkedMovementPush's
        // own comment for why the actual push (a mutating Physics call
        // gated on RigidBodyMotionType::Dynamic) has to happen one layer up,
        // in NetworkSession, not here.
        if (outPush != nullptr && blocked) {
            outPush->entity = blockEntity;
            outPush->direction = moveDir;
            outPush->strength = horizontalDist - allowedDist;
        }

        // Real collide-and-slide: the ray fan above only proves "don't
        // tunnel through the wall" -- clamping to `allowedDist` alone just
        // discards the rest of the move, which is exactly what read as
        // "getting stuck/snagging" pushing into a wall at any angle other
        // than dead-on. Instead, project the leftover distance (the part
        // the blocker ate) onto the plane tangent to the wall's own
        // surface normal (flattened to horizontal, since this is a
        // horizontal-only probe) and apply that as extra movement, same
        // "velocity minus its component along the normal" projection every
        // standard character controller uses for wall sliding.
        if (blocked && allowedDist < horizontalDist) {
            glm::vec3 flatNormal(blockNormal.x, 0.0f, blockNormal.z);
            if (glm::length(flatNormal) > 0.0001f) {
                flatNormal = glm::normalize(flatNormal);
                glm::vec3 remaining = moveDir * (horizontalDist - allowedDist);
                glm::vec3 slide = remaining - glm::dot(remaining, flatNormal) * flatNormal;
                float slideDist = glm::length(slide);
                if (slideDist > 0.0001f) {
                    glm::vec3 slideDir = slide / slideDist;
                    // One more real check -- don't let the slide itself
                    // tunnel into a second, adjacent obstacle (e.g. an
                    // inside corner) the primary fan above never probed.
                    core::Physics::RaycastHit slideBlocker =
                        physics.raycast(transform.position + primaryDelta, slideDir, slideDist + kCapsuleRadius);
                    if (slideBlocker.hit) {
                        slideDist = std::min(slideDist, std::max(0.0f, slideBlocker.distance - kCapsuleRadius));
                    }
                    horizontalDelta += slideDir * slideDist;
                }
            }
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
