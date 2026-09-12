#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "core/Camera.hpp"
#include "core/CharacterController.hpp"
#include "core/ECS.hpp"
#include "core/Physics.hpp"

namespace engine::despair {

// PROJECT: DESPAIR -- Agent David Miller's first-person view. Deliberately
// does not reimplement movement/acceleration/step-up/jump (core::
// CharacterController already has all of that, shared with every other
// scene) -- this file only adds what a first-person horror game needs on
// top: a zero-distance eye camera, a full yaw+pitch Transform::rotation
// SanitySystem's gaze detection and StalkerAI's playerLooksAtStalker both
// require (see SanitySystem.cpp's own comment), and PlayerNoiseLevel driven
// by real resolved speed.
//
// FPSPlayerSettings on the character entity is the seam, same convention
// every other DESPAIR system in Application.cpp already uses -- absent it,
// characterController_ behaves exactly as it always has for every
// non-DESPAIR scene.
struct FPSPlayerSettings {
    // No true crouch (capsule half-height change) in this vertical slice --
    // core::CharacterController has no crouch concept, and adding one is
    // real scope this pass isn't taking on. "Crouched" is honestly modeled
    // as speed-and-noise-only: while the Crouch action is held, noise is
    // forced to crouchNoiseLevel regardless of whatever speed the capsule
    // still resolves.
    float walkNoiseLevel = 0.35f;
    float runNoiseLevel = 1.0f;
    float crouchNoiseLevel = 0.0f;
};

// One-time-per-call, idempotent settings mutation that turns `controller`'s
// existing third-person orbit cam into a first-person "at the eyes, no lag"
// view: zero orbit distance, eye-height focus, zero position smoothing.
// Cheap enough (three float writes) to call unconditionally every tick
// alongside FPSPlayerSettings' presence check rather than needing its own
// one-shot latch. Reuses every other piece of CharacterController::tick()
// (movement, step-up, jump, mouse-look) completely unmodified.
void configureFirstPersonCamera(core::CharacterController& controller);

// Skin distance added on top of capsuleRadius by computeInteractionRayOrigin
// below -- pure margin against float precision at the capsule surface, same
// role kSkin plays in CharacterController::tryStepUp().
inline constexpr float kInteractionRayOriginSkin = 0.05f;

// Pure. In first-person, configureFirstPersonCamera() puts camera.position
// *inside* the character's own capsule (unlike a third-person orbit cam,
// which sits outside it at cameraDistance) -- a raycast fired straight from
// there self-hits the capsule, and Jolt's CastRay returns only the single
// closest hit with no exclusion filter this engine's wrapper exposes, so
// that self-hit can never be seen past. Nudging the origin forward along
// camera.forward() by capsuleRadius + kInteractionRayOriginSkin clears the
// capsule surface before the cast starts, the same "start the ray outside
// the capsule's own collision volume" convention
// CharacterController::tryStepUp() already uses. Callers that use this must
// also shorten their max cast distance by the same offset so total reach
// from the true eye position is unchanged. One source of truth for this
// geometry, shared by Application.cpp's interaction raycast and its own
// regression test -- previously spelled out independently in both places,
// which is exactly how the center-vs-feet cameraHeight bug this file also
// fixes went unnoticed.
[[nodiscard]] glm::vec3 computeInteractionRayOrigin(const core::Camera& camera, float capsuleRadius);

// Pure. Builds the full yaw+pitch look-direction quaternion SanitySystem's
// gaze detection and StalkerAI's playerLooksAtStalker both require --
// `rotation * (0,0,-1) == camera.forward()`, matching the "-Z-at-identity"
// convention SanitySystem.cpp's own comment documents. Built from
// glm::quatLookAt(camera.forward(), up), the same real quaternion-from-
// forward-vector construction cinematic::CameraRail already uses -- not
// hand-rolled trig that could silently drift from Camera::forward()'s own
// formula.
[[nodiscard]] glm::quat computeLookRotation(const core::Camera& camera);

// Pure. Derives PlayerNoiseLevel::current from real resolved horizontal
// speed (not input flags -- see this file's own .cpp comment), piecewise-
// linear across [0, walkSpeed] -> [0, walkNoiseLevel] and
// [walkSpeed, runSpeed] -> [walkNoiseLevel, runNoiseLevel], clamped at the
// ends. `crouching` overrides the speed-based result outright with
// crouchNoiseLevel.
[[nodiscard]] float computeNoiseLevel(const FPSPlayerSettings& settings, float horizontalSpeed, float walkSpeed,
                                       float runSpeed, bool crouching);

// Real ECS/Physics-touching glue. Call once per tick from the
// postPhysicsHook -- after Physics::step()'s syncTransforms() has already
// reset the capsule's Transform::rotation to yaw-only for this tick, and
// before despairAiManager_.update() reads it (see this file's own .cpp
// comment for why the ordering is load-bearing). A real, honest no-op if
// `character` carries no FPSPlayerSettings.
void updateFirstPersonPlayer(core::ECS& ecs, core::Physics& physics, core::EntityId character,
                              const core::Camera& camera, const core::CharacterController::Settings& controllerSettings,
                              bool crouching);

} // namespace engine::despair
