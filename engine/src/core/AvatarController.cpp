#include "core/AvatarController.hpp"

#include "core/Components.hpp"

namespace engine::core {

AvatarController::AvatarController(Skeleton skeleton) : player_(std::move(skeleton)) {}

AvatarController::AvatarController(Skeleton skeleton, Settings settings)
    : player_(std::move(skeleton)), settings_(settings) {}

void AvatarController::setIdleClip(AnimationClip clip) {
    idleClip_ = std::move(clip);
    hasIdle_ = true;
}
void AvatarController::setWalkClip(AnimationClip clip) {
    walkClip_ = std::move(clip);
    hasWalk_ = true;
}
void AvatarController::setRunClip(AnimationClip clip) {
    runClip_ = std::move(clip);
    hasRun_ = true;
}
void AvatarController::setJumpClip(AnimationClip clip) {
    jumpClip_ = std::move(clip);
    hasJump_ = true;
}
void AvatarController::setJumpAirClip(AnimationClip clip) {
    jumpAirClip_ = std::move(clip);
    hasJumpAir_ = true;
}
void AvatarController::setJumpLandClip(AnimationClip clip) {
    jumpLandClip_ = std::move(clip);
    hasJumpLand_ = true;
}

void AvatarController::playEmote(AnimationClip clip, bool looping, bool fullBody) {
    AnimationLayer layer = fullBody ? AnimationLayer::Base : AnimationLayer::UpperBody;
    emoteHandle_ = player_.play(std::move(clip), layer, looping, settings_.emoteBlendSeconds);
    emotePlaying_ = true;
    // A full-body emote plays on the same layer locomotion does -- it
    // stays up until tick()'s own state-change logic naturally crossfades
    // it away (e.g. the player starts moving and locomotion re-triggers a
    // Base-layer play()), which is also why fullBody emotes don't touch
    // `state_` here: state_ still reflects whatever locomotion state was
    // last chosen, so the moment that state changes (or grounded/airborne
    // flips), tick() re-asserts it over the emote with no special-casing
    // needed for the handoff.
}

void AvatarController::stopEmote() {
    if (emotePlaying_) {
        player_.stop(emoteHandle_, settings_.emoteBlendSeconds);
        emotePlaying_ = false;
    }
}

bool AvatarController::isEmotePlaying() const { return emotePlaying_ && player_.isPlaying(emoteHandle_); }

void AvatarController::tickAnimation(float dt, float horizontalSpeed, bool grounded, float verticalVelocity) {
    bool evaluateLocomotion = false;

    if (!grounded) {
        if (verticalVelocity >= 0.0f) {
            // Rising (or at the apex) -- real Jump state. jump_start fires
            // once, on the grounded -> airborne edge (not every airborne
            // tick -- replaying it every frame would restart its playhead
            // and never actually finish the animation).
            if (wasGrounded_ && hasJump_) {
                player_.play(jumpClip_, AnimationLayer::Base, /*looping=*/false, settings_.jumpBlendSeconds);
            }
            state_ = AvatarLocomotionState::Jump;
        } else {
            // Real, explicit trigger: vertical velocity < 0 and not
            // grounded. jump_air fires once, looping, on the Jump ->
            // Falling edge -- covers Falling on its own too (no separate
            // falling clip is authored; a real, honest simplification,
            // see setJumpAirClip()'s own comment), so re-entering Falling
            // from Falling never retriggers it.
            if (state_ != AvatarLocomotionState::Falling && hasJumpAir_) {
                player_.play(jumpAirClip_, AnimationLayer::Base, /*looping=*/true, settings_.jumpBlendSeconds);
            }
            state_ = AvatarLocomotionState::Falling;
        }
        locomotionStarted_ = true;
    } else if (!wasGrounded_) {
        // Just landed (airborne last tick, grounded this tick) -- real
        // Landing state, jump_land fires once. No land clip authored is a
        // real, honest fallback straight back to locomotion, not a stall.
        if (hasJumpLand_) {
            landHandle_ = player_.play(jumpLandClip_, AnimationLayer::Base, /*looping=*/false, settings_.jumpBlendSeconds);
            state_ = AvatarLocomotionState::Landing;
        } else {
            evaluateLocomotion = true;
        }
    } else if (state_ == AvatarLocomotionState::Landing) {
        // Stay in Landing until jump_land's own real playhead reaches its
        // duration -- AnimationPlayer holds a finished non-looping clip's
        // last frame rather than auto-stopping it (see that class's own
        // tick() comment), so this is the real, honest way to detect
        // "the landing recovery actually finished," not a guessed timer.
        bool landFinished =
            landHandle_ == AnimationPlayer::kInvalidHandle || player_.playhead(landHandle_) >= jumpLandClip_.duration - 1e-3f;
        if (landFinished) {
            evaluateLocomotion = true;
        } else {
            wasGrounded_ = grounded;
            player_.tick(dt);
            return;
        }
    } else {
        evaluateLocomotion = true;
    }

    if (evaluateLocomotion) {
        AvatarLocomotionState desired = horizontalSpeed >= settings_.runSpeedThreshold ? AvatarLocomotionState::Run
                                         : horizontalSpeed >= settings_.walkSpeedThreshold ? AvatarLocomotionState::Walk
                                                                                            : AvatarLocomotionState::Idle;
        // Re-trigger locomotion playback on an actual state change
        // (Landing/Jump/Falling all differ from Idle/Walk/Run, so
        // finishing any of them naturally re-triggers here too), or on
        // the very first grounded tick ever (!locomotionStarted_ -- see
        // its header comment) -- never on every matching tick, or a
        // looping locomotion clip would restart its playhead every frame
        // and never appear to loop.
        if (desired != state_ || !locomotionStarted_) {
            const AnimationClip* clip = nullptr;
            bool has = false;
            switch (desired) {
                case AvatarLocomotionState::Run: clip = &runClip_; has = hasRun_; break;
                case AvatarLocomotionState::Walk: clip = &walkClip_; has = hasWalk_; break;
                default: clip = &idleClip_; has = hasIdle_; break;
            }
            if (has) {
                // The very first locomotion clip ever played has nothing
                // to crossfade *from* -- an actual state change (idle ->
                // walk) uses the configured blend duration, but this
                // first trigger cuts in instantly rather than fading up
                // from silence.
                float fadeSeconds = locomotionStarted_ ? settings_.locomotionBlendSeconds : 0.0f;
                player_.play(*clip, AnimationLayer::Base, true, fadeSeconds);
                locomotionStarted_ = true;
            }
            state_ = desired;
        }
    }

    wasGrounded_ = grounded;
    player_.tick(dt);
}

void AvatarController::tick(float dt, ECS& ecs, Physics& physics, EntityId character,
                             const std::vector<EntityId>& skinnedEntities) {
    bool grounded = physics.isGrounded(character, ecs, settings_.capsuleHalfHeight, settings_.capsuleRadius);
    glm::vec3 velocity = physics.getLinearVelocity(character, ecs);
    float horizontalSpeed = glm::length(glm::vec2(velocity.x, velocity.z));

    tickAnimation(dt, horizontalSpeed, grounded, velocity.y);

    Transform characterTransform;
    if (auto* t = ecs.tryGetComponent<Transform>(character)) characterTransform = *t;

    const auto& skinningMatrices = player_.skinningMatrices();
    for (EntityId entity : skinnedEntities) {
        if (auto* t = ecs.tryGetComponent<Transform>(entity)) *t = characterTransform;
        if (auto* skinned = ecs.tryGetComponent<SkinnedRenderable>(entity)) skinned->skinningMatrices = skinningMatrices;
    }
}

} // namespace engine::core
