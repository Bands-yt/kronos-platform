#include "despair/FPSPlayerController.hpp"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

#include <algorithm>

#include "core/Components.hpp"
#include "despair/EscapeGameLoop.hpp"
#include "despair/HorrorAIManager.hpp"
#include "despair/SanitySystem.hpp"

namespace engine::despair {

void configureFirstPersonCamera(core::CharacterController& controller) {
    core::CharacterController::Settings& settings = controller.settingsMutable();
    settings.cameraDistance = 0.0f;
    // Settings::cameraHeight is added to Transform::position, and for a
    // physics capsule that's the Jolt body's *center* (see
    // CharacterController::tick()'s own `footY = center.y - capsuleHalfHeight
    // - capsuleRadius`), not the feet -- unlike kPlayerEyeHeight, which
    // HorrorAIManager defines as a feet-relative lift. Subtracting the
    // center-to-feet offset converts kPlayerEyeHeight into the right
    // center-relative value so the eye actually ends up at feet +
    // kPlayerEyeHeight, matching what HorrorAIManager's line-of-sight raycasts
    // assume the player can see from.
    settings.cameraHeight = kPlayerEyeHeight - (settings.capsuleHalfHeight + settings.capsuleRadius);
    settings.cameraPositionSmoothing = 0.0f;
}

glm::vec3 computeInteractionRayOrigin(const core::Camera& camera, float capsuleRadius) {
    return camera.position + camera.forward() * (capsuleRadius + kInteractionRayOriginSkin);
}

glm::quat computeLookRotation(const core::Camera& camera) {
    return glm::quatLookAt(camera.forward(), glm::vec3(0.0f, 1.0f, 0.0f));
}

float computeNoiseLevel(const FPSPlayerSettings& settings, float horizontalSpeed, float walkSpeed, float runSpeed,
                         bool crouching) {
    if (crouching) return settings.crouchNoiseLevel;

    float speed = std::max(horizontalSpeed, 0.0f);
    float walkDenominator = std::max(walkSpeed, 0.0001f);
    if (speed <= walkSpeed) {
        return settings.walkNoiseLevel * std::clamp(speed / walkDenominator, 0.0f, 1.0f);
    }

    float runDenominator = std::max(runSpeed - walkSpeed, 0.0001f);
    float t = std::clamp((speed - walkSpeed) / runDenominator, 0.0f, 1.0f);
    return settings.walkNoiseLevel + t * (settings.runNoiseLevel - settings.walkNoiseLevel);
}

void updateFirstPersonPlayer(core::ECS& ecs, core::Physics& physics, core::EntityId character,
                              const core::Camera& camera, const core::CharacterController::Settings& controllerSettings,
                              bool crouching) {
    auto* fpsSettings = ecs.tryGetComponent<FPSPlayerSettings>(character);
    if (fpsSettings == nullptr) return;

    // Real, honest lazy-attach -- same convention KeycardInventory/
    // PlayerHidingState already use in Application.cpp's interaction
    // cascade -- so nothing outside FPSPlayerController needs its own
    // "spawn the player's sanity/noise components" step.
    if (ecs.tryGetComponent<SanityState>(character) == nullptr) ecs.addComponent<SanityState>(character);
    auto* noise = ecs.tryGetComponent<PlayerNoiseLevel>(character);
    if (noise == nullptr) noise = &ecs.addComponent<PlayerNoiseLevel>(character);
    if (ecs.tryGetComponent<EscapeGameState>(character) == nullptr) ecs.addComponent<EscapeGameState>(character);

    // Raw ECS write, deliberately never routed through any
    // Physics::setRotation*() call: the capsule's Jolt body is pitch/roll-
    // locked (see Physics::createCharacterCapsule()'s own comment), so full
    // pitch can never be a physically simulated rotation. Physics::step()'s
    // syncTransforms() already overwrote this tick's Transform::rotation
    // back to yaw-only just before this function runs (see
    // GameLoop::setPostPhysicsHook's ordering) -- this write is what makes
    // it authoritative again for the rest of this tick, until the next
    // syncTransforms() resets it and this function immediately re-
    // establishes it once more.
    if (auto* transform = ecs.tryGetComponent<core::Transform>(character)) {
        transform->rotation = computeLookRotation(camera);
    }

    glm::vec3 velocity = physics.getLinearVelocity(character, ecs);
    float horizontalSpeed = glm::length(glm::vec2(velocity.x, velocity.z));
    noise->current = computeNoiseLevel(*fpsSettings, horizontalSpeed, controllerSettings.walkSpeed,
                                        controllerSettings.runSpeed, crouching);
}

} // namespace engine::despair
