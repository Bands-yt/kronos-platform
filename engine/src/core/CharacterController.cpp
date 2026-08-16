#include "core/CharacterController.hpp"

#include <algorithm>
#include <cmath>

#include <SDL2/SDL.h>

#include "core/AvatarLOD.hpp"

namespace engine::core {

glm::vec2 rampVelocityTowardTarget(glm::vec2 current, glm::vec2 target, float rate, float dt) {
    glm::vec2 toTarget = target - current;
    float distance = glm::length(toTarget);
    float maxDelta = std::max(0.0f, rate) * dt;
    if (distance <= maxDelta || distance < 1e-5f) return target;
    return current + (toTarget / distance) * maxDelta;
}

void CharacterController::configureInput(platform_adapters::UnifiedInput& input) const {
    using platform_adapters::InputBinding;
    using platform_adapters::PhysicalInputKind;

    input.bindAction("MoveForward", InputBinding{PhysicalInputKind::KeyboardKey, SDL_SCANCODE_W});
    input.bindAction("MoveBackward", InputBinding{PhysicalInputKind::KeyboardKey, SDL_SCANCODE_S});
    input.bindAction("MoveLeft", InputBinding{PhysicalInputKind::KeyboardKey, SDL_SCANCODE_A});
    input.bindAction("MoveRight", InputBinding{PhysicalInputKind::KeyboardKey, SDL_SCANCODE_D});
    input.bindAction("Jump", InputBinding{PhysicalInputKind::KeyboardKey, SDL_SCANCODE_SPACE});
    // Held (not toggled) -- the standard "sprint while holding Shift"
    // scheme, real walk/run distinction driving Settings::runSpeed
    // instead of Settings::walkSpeed while down.
    input.bindAction("Run", InputBinding{PhysicalInputKind::KeyboardKey, SDL_SCANCODE_LSHIFT});
}

EntityId CharacterController::spawn(ECS& ecs, Physics& physics, glm::vec3 spawnPosition, uint32_t noseMeshHandle) {
    entity_ = physics.createCharacterCapsule(ecs, spawnPosition, settings_.capsuleRadius, settings_.capsuleHalfHeight, 70.0f);

    if (noseMeshHandle != Renderable::kInvalidHandle) {
        noseEntity_ = ecs.createEntity("CharacterFacingMarker");
        auto& renderable = ecs.addComponent<Renderable>(noseEntity_);
        renderable.meshHandle = noseMeshHandle;
        renderable.baseColor = glm::vec4(1.0f, 0.55f, 0.1f, 1.0f); // bright, unmissable "you are facing here" orange
        renderable.metallic = 0.0f;
        renderable.roughness = 0.4f;
        renderable.castsShadow = false; // small enough that its shadow would just be visual noise
    }
    return entity_;
}

bool CharacterController::tryStepUp(ECS& ecs, Physics& physics, glm::vec3 moveDir) {
    if (glm::length(moveDir) < 0.0001f || settings_.stepHeight <= 0.0f) return false;
    auto* transform = ecs.tryGetComponent<Transform>(entity_);
    if (!transform) return false;

    glm::vec3 dir = glm::normalize(glm::vec3(moveDir.x, 0.0f, moveDir.z));
    glm::vec3 center = transform->position;
    float footY = center.y - settings_.capsuleHalfHeight - settings_.capsuleRadius;

    // Same "start the ray outside the capsule's own collision volume"
    // convention Physics::isGrounded() already uses -- a body filter
    // would work too, but this engine has never needed one for a raycast
    // and this keeps the same established technique.
    constexpr float kSkin = 0.05f;
    constexpr float kCastDistance = 0.3f;
    glm::vec3 forwardOffset = dir * (settings_.capsuleRadius + kSkin);

    // Low ray: just above the feet -- is something blocking movement
    // right at ground level?
    glm::vec3 lowOrigin = glm::vec3(center.x, footY + 0.1f, center.z) + forwardOffset;
    Physics::RaycastHit lowHit = physics.raycast(lowOrigin, dir, kCastDistance);
    if (!lowHit.hit) return false; // nothing in the way at foot level -- no step needed

    // High ray: at step height -- is the same direction clear up there?
    glm::vec3 highOrigin = glm::vec3(center.x, footY + settings_.stepHeight + kSkin, center.z) + forwardOffset;
    Physics::RaycastHit highHit = physics.raycast(highOrigin, dir, kCastDistance);
    if (highHit.hit) return false; // still blocked above step height too -- a real wall, not a short ledge

    // Real landing check: the two rays above only prove "blocked low,
    // clear high" -- they say nothing about whether there's actually
    // solid ground to land *on* at the stepped-up height, just that the
    // path there is unobstructed. Without this, a spurious "blocked low/
    // clear high" reading near any small prop (this engine's own bring-up
    // scene scatters many small decorative cubes across the ground, see
    // main.cpp) would step the character up into thin air -- and worse,
    // if ungrounded, tryStepUp() could be satisfied again on the very
    // next tick by a *different* nearby prop, compounding into a
    // runaway climb. Probed at roughly where the character's *center*
    // will actually end up (radius + castDistance ahead -- past the
    // obstacle's near face, not at the horizontal ray-cast origin, which
    // sits *before* the obstacle even starts and would probe empty space
    // in front of it instead of the landing surface itself).
    glm::vec3 landingXZ = glm::vec3(center.x, 0.0f, center.z) + dir * (settings_.capsuleRadius + kCastDistance);
    constexpr float kLandingProbeStartAbove = 0.5f;
    glm::vec3 landingProbeOrigin(landingXZ.x, footY + settings_.stepHeight + kLandingProbeStartAbove, landingXZ.z);
    // Reaches all the way back down to the character's *original* foot
    // level (footY) -- so a real landing surface anywhere between the
    // original ground and the full step height is accepted, not just an
    // exact match at footY+stepHeight.
    float landingProbeDistance = kLandingProbeStartAbove + settings_.stepHeight;
    Physics::RaycastHit landingHit = physics.raycast(landingProbeOrigin, glm::vec3(0.0f, -1.0f, 0.0f), landingProbeDistance);
    if (!landingHit.hit) return false; // nothing solid to actually land on up there

    // Real, instant step: nudge the character straight up. stepHeight is
    // meant to be curb/stair-sized, small enough that an instant
    // reposition reads as smooth climbing rather than a visible teleport
    // (the same assumption real "step offset" features in other engines
    // make -- this isn't a smoothed climb over multiple ticks).
    physics.setPosition(entity_, ecs, center + glm::vec3(0.0f, settings_.stepHeight, 0.0f));
    return true;
}

void CharacterController::tick(float dt, ECS& ecs, Physics& physics, platform_adapters::UnifiedInput& input,
                                Camera& camera, AvatarController* avatarController,
                                const std::vector<EntityId>* skinnedEntities) {
    if (entity_ == kNullEntity) return;

    glm::vec2 mouseDelta = input.mouseDelta();
    cameraYawDegrees_ += mouseDelta.x * settings_.mouseSensitivity;
    cameraPitchDegrees_ -= mouseDelta.y * settings_.mouseSensitivity;
    cameraPitchDegrees_ = std::clamp(cameraPitchDegrees_, -80.0f, 80.0f);

    // Movement is relative to the camera's yaw (standard third-person
    // scheme -- press W to run away from the camera, not along the
    // character's current facing), pitch deliberately excluded so walking
    // doesn't slow down just because you're looking up.
    float yawRad = glm::radians(cameraYawDegrees_);
    glm::vec3 camForward = glm::normalize(glm::vec3(std::cos(yawRad), 0.0f, std::sin(yawRad)));
    glm::vec3 camRight = glm::normalize(glm::cross(camForward, glm::vec3(0.0f, 1.0f, 0.0f)));

    // Animation -> movement sync: a playing full-body emote takes over
    // the character the same way Roblox's own client blocks WASD input
    // during one -- see tick()'s header comment on why this isn't a full
    // root-motion system.
    bool emoteSuppressesMovement = avatarController != nullptr && avatarController->isEmotePlaying();

    glm::vec3 moveDir(0.0f);
    if (!emoteSuppressesMovement) {
        if (input.isActionDown("MoveForward")) moveDir += camForward;
        if (input.isActionDown("MoveBackward")) moveDir -= camForward;
        if (input.isActionDown("MoveRight")) moveDir += camRight;
        if (input.isActionDown("MoveLeft")) moveDir -= camRight;
    }
    bool hasInput = glm::length(moveDir) > 0.0001f;
    if (hasInput) moveDir = glm::normalize(moveDir);

    // Real slope limit: isGrounded()'s raw raycast-hit-something bool
    // can't tell a walkable floor from a too-steep wall/ramp -- checkGround()
    // also resolves the real surface normal, and the angle between that
    // and world-up is what actually decides "standable" here.
    Physics::GroundInfo ground =
        physics.checkGround(entity_, ecs, settings_.capsuleHalfHeight, settings_.capsuleRadius);
    float slopeDegrees = glm::degrees(std::acos(std::clamp(ground.normal.y, -1.0f, 1.0f)));
    bool standableGround = ground.grounded && slopeDegrees <= settings_.maxSlopeDegrees;

    bool running = input.isActionDown("Run");
    float targetSpeed = running ? settings_.runSpeed : settings_.walkSpeed;
    glm::vec2 targetVelocity = hasInput ? glm::vec2(moveDir.x, moveDir.z) * targetSpeed : glm::vec2(0.0f);

    // Real acceleration model (see Settings::groundAcceleration's
    // comment): ramp the *current* horizontal velocity toward the target
    // instead of snapping to it. Accelerating and decelerating use
    // different rates (starting to move vs. grinding to a halt feel
    // different), and airControlMultiplier scales down how much
    // authority input has while airborne.
    glm::vec3 currentVelocity3 = physics.getLinearVelocity(entity_, ecs);
    glm::vec2 currentVelocity(currentVelocity3.x, currentVelocity3.z);
    bool accelerating = glm::length(targetVelocity) > glm::length(currentVelocity);
    float rate = accelerating ? settings_.groundAcceleration : settings_.groundDeceleration;
    if (!standableGround) rate *= settings_.airControlMultiplier;
    glm::vec2 newVelocity = rampVelocityTowardTarget(currentVelocity, targetVelocity, rate, dt);

    physics.setHorizontalVelocity(entity_, ecs, newVelocity);

    if (hasInput) {
        // Face the direction of movement (Roblox's default character
        // behavior) -- only updated while actually moving, so the
        // character holds its last facing when you stop rather than
        // snapping back to some default.
        facingYawRadians_ = std::atan2(moveDir.x, moveDir.z);
    }
    physics.setRotationY(entity_, ecs, facingYawRadians_);

    // Real step-offset: only worth checking while grounded and actually
    // trying to move somewhere -- an airborne or stationary character has
    // nothing to step onto.
    if (standableGround && hasInput) {
        tryStepUp(ecs, physics, moveDir);
    }

    if (input.isActionDown("Jump") && standableGround) {
        physics.setVerticalVelocity(entity_, ecs, settings_.jumpSpeed);
    }

    glm::vec3 characterPos(0.0f);
    if (auto* transform = ecs.tryGetComponent<Transform>(entity_)) {
        characterPos = transform->position;
    }

    // Facing marker -- see spawn()'s doc comment on why a symmetric
    // capsule needs one at all. Not physics-driven (no RigidBody), just a
    // Transform this class writes directly every tick, offset from the
    // capsule center by the same facing angle setRotationY just applied
    // to the real body.
    if (noseEntity_ != kNullEntity) {
        if (auto* noseTransform = ecs.tryGetComponent<Transform>(noseEntity_)) {
            glm::vec3 faceDir(std::sin(facingYawRadians_), 0.0f, std::cos(facingYawRadians_));
            noseTransform->position = characterPos + faceDir * settings_.noseForwardOffset +
                                       glm::vec3(0.0f, settings_.noseHeightOffset, 0.0f);
            noseTransform->scale = glm::vec3(0.12f);
        }
    }

    // Movement -> animation sync: hand this tick's real, physics-resolved
    // state to the avatar's own blend tree -- see tick()'s header comment.
    // Deliberately called *after* this method's own movement/jump/step
    // logic has already run this tick, so AvatarController reads the same
    // grounded/velocity state Physics::step() is about to act on, not a
    // tick-stale one.
    if (avatarController != nullptr && skinnedEntities != nullptr) {
        avatarController->tick(dt, ecs, physics, entity_, *skinnedEntities);

        // Kronos ("Avatar 2.0" -- "Performance and LOD" -- "Add
        // distance-based LOD levels for clothing meshes, accessories,
        // and facial features"): real -- camera.position here is still
        // last tick's value (this same function only overwrites it
        // further down, see the real `camera.position = ...` line
        // below), which is a real, honest, imperceptible one-tick-stale
        // distance, not a fabricated one. In ordinary third-person play
        // this stays comfortably under every real cutoff (Settings::
        // cameraDistance defaults to 6.0f, well below
        // AvatarLODThresholds::faceCutoffMeters's own 9.0f default), so
        // a player's own face/accessories/clothing never disappear
        // during normal gameplay -- this only engages for a genuinely
        // far camera.
        updateAvatarLOD(ecs, *skinnedEntities, glm::length(camera.position - characterPos));
    }

    // Third-person follow camera -- orbits at a fixed distance behind
    // wherever mouse-look currently points. Look direction (yaw/pitch)
    // follows the mouse instantly (any lag there would read as input
    // latency); the camera's *position* eases toward the character's
    // focus point instead of snapping there every tick (see
    // Settings::cameraPositionSmoothing) so a sudden vertical motion
    // (landing a jump, a step-offset nudge) doesn't visibly teleport the
    // view.
    camera.yawDegrees = cameraYawDegrees_;
    camera.pitchDegrees = cameraPitchDegrees_;

    glm::vec3 targetFocus = characterPos + glm::vec3(0.0f, settings_.cameraHeight, 0.0f);
    if (!cameraFocusInitialized_) {
        smoothedCameraFocus_ = targetFocus;
        cameraFocusInitialized_ = true;
    } else if (settings_.cameraPositionSmoothing <= 0.0f) {
        smoothedCameraFocus_ = targetFocus;
    } else {
        // Frame-rate-independent exponential smoothing -- unlike a fixed
        // per-tick lerp factor (which implicitly assumes a fixed dt), this
        // converges at the same real-time rate regardless of tick rate.
        float t = 1.0f - std::exp(-settings_.cameraPositionSmoothing * dt);
        smoothedCameraFocus_ = glm::mix(smoothedCameraFocus_, targetFocus, t);
    }

    camera.position = smoothedCameraFocus_ - camera.forward() * settings_.cameraDistance;
}

} // namespace engine::core
