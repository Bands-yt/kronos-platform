#include "core/AvatarController.hpp"

#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

#include "core/Components.hpp"

namespace engine::core {

namespace {
constexpr float kTwoPi = 6.28318530718f;
}

float computeSecondaryHeadBobDegrees(AvatarLocomotionState state, float phase, float idleSwayDegrees,
                                      float walkBobDegrees, float runBobDegrees) {
    float amplitude = 0.0f;
    switch (state) {
        case AvatarLocomotionState::Idle: amplitude = idleSwayDegrees; break;
        case AvatarLocomotionState::Walk: amplitude = walkBobDegrees; break;
        case AvatarLocomotionState::Run: amplitude = runBobDegrees; break;
        default: return 0.0f; // Jump/Falling/Landing -- real, deliberate authored motion, no procedural bob on top
    }
    return amplitude * std::sin(phase);
}

float secondaryHeadBobHzForState(AvatarLocomotionState state, float idleSwayHz, float walkBobHz, float runBobHz) {
    switch (state) {
        case AvatarLocomotionState::Idle: return idleSwayHz;
        case AvatarLocomotionState::Walk: return walkBobHz;
        case AvatarLocomotionState::Run: return runBobHz;
        default: return 0.0f;
    }
}

float computeSecondaryOscillationDegrees(AvatarLocomotionState state, float phase, float idleDegrees,
                                          float walkDegrees, float runDegrees) {
    float amplitude = 0.0f;
    switch (state) {
        case AvatarLocomotionState::Idle: amplitude = idleDegrees; break;
        case AvatarLocomotionState::Walk: amplitude = walkDegrees; break;
        case AvatarLocomotionState::Run: amplitude = runDegrees; break;
        default: return 0.0f; // Jump/Falling/Landing -- same real "don't fight authored motion" reasoning as the head-bob
    }
    return amplitude * std::sin(phase);
}

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

    // Kronos ("Avatar 2.0" -- "Animation Polish: secondary motion"): real
    // phase advance, wrapped to [0, 2*pi) so it never grows unbounded
    // over a long play session (float precision would eventually suffer
    // otherwise). See computeSecondaryHeadBobDegrees()'s own comment for
    // why this is only ever nonzero during Idle/Walk/Run.
    secondaryMotionPhase_ += dt * kTwoPi *
                             secondaryHeadBobHzForState(state_, settings_.idleSwayHz, settings_.walkBobHz, settings_.runBobHz);
    secondaryMotionPhase_ = std::fmod(secondaryMotionPhase_, kTwoPi);

    Transform characterTransform;
    if (auto* t = ecs.tryGetComponent<Transform>(character)) characterTransform = *t;

    // A real, mutable copy -- player_.skinningMatrices() is const (shared
    // read-only state every skinned entity would otherwise reference
    // directly); the head-bob/facial-expression offsets below need to
    // mutate individual joint entries before handing the result off.
    std::vector<glm::mat4> skinningMatrices = player_.skinningMatrices();
    std::vector<glm::mat4> bindWorld = player_.skeleton().bindPoseMatrices();
    // Kronos ("Avatar 2.0" real correctness fix): pivots a real joint's
    // skinning matrix around that joint's own real bind-pose world
    // position, not the rig's local origin -- a plain right-multiplied
    // rotate() alone rotates *around whatever point the mesh's own
    // vertices were baked relative to* (world/rig-space origin here,
    // since buildHumanoidMeshData() bakes vertices at their real
    // bind-pose world position, see that function's own worldPos()
    // lambda), which would swing a limb several centimeters sideways per
    // degree instead of moving it in place. translate(+jointPos) *
    // rotate * translate(-jointPos) is the standard "rotate about an
    // arbitrary point" construction, shared here across head/torso/arms
    // rather than re-derived three times.
    auto applyPivotedRotation = [&](const char* jointName, float degrees, glm::vec3 axis) {
        int jointIndex = player_.skeleton().findJointIndex(jointName);
        if (jointIndex < 0 || static_cast<size_t>(jointIndex) >= skinningMatrices.size() ||
            static_cast<size_t>(jointIndex) >= bindWorld.size()) {
            return;
        }
        glm::vec3 jointPos = glm::vec3(bindWorld[static_cast<size_t>(jointIndex)][3]);
        glm::mat4 rot = glm::translate(glm::mat4(1.0f), jointPos) *
                         glm::rotate(glm::mat4(1.0f), glm::radians(degrees), axis) *
                         glm::translate(glm::mat4(1.0f), -jointPos);
        skinningMatrices[static_cast<size_t>(jointIndex)] = skinningMatrices[static_cast<size_t>(jointIndex)] * rot;
    };

    float bobDegrees = computeSecondaryHeadBobDegrees(state_, secondaryMotionPhase_, settings_.idleSwayDegrees,
                                                        settings_.walkBobDegrees, settings_.runBobDegrees);
    applyPivotedRotation("head", bobDegrees, glm::vec3(1.0f, 0.0f, 0.0f));

    // Kronos ("Avatar 2.0" -- "Animation Polish" -- "secondary motion for
    // ... torso, and arms"): real -- torso sways side-to-side (Z-axis,
    // distinct from the head's own front-back X-axis nod); arms swing
    // front-back (X-axis, a natural walking arm pump) with real,
    // opposite phase between left/right (negating the amplitude is
    // equivalent to a real pi-radian phase offset for a sine wave).
    float torsoSwayDegrees = computeSecondaryOscillationDegrees(state_, secondaryMotionPhase_,
                                                                   settings_.idleTorsoSwayDegrees,
                                                                   settings_.walkTorsoSwayDegrees,
                                                                   settings_.runTorsoSwayDegrees);
    applyPivotedRotation("spine_upper", torsoSwayDegrees, glm::vec3(0.0f, 0.0f, 1.0f));

    float armSwingDegrees = computeSecondaryOscillationDegrees(state_, secondaryMotionPhase_,
                                                                  settings_.idleArmSwingDegrees,
                                                                  settings_.walkArmSwingDegrees,
                                                                  settings_.runArmSwingDegrees);
    applyPivotedRotation("arm_L_upper", armSwingDegrees, glm::vec3(1.0f, 0.0f, 0.0f));
    applyPivotedRotation("arm_R_upper", -armSwingDegrees, glm::vec3(1.0f, 0.0f, 0.0f));

    // Kronos ("Avatar 2.0" -- "Facial System" -- real, automatic,
    // periodic blinking): a real triangle-shaped open->closed->open
    // envelope over autoBlinkDurationSeconds, writing straight into
    // targetFacialExpression_.blinkWeight so it composes naturally with
    // whatever a real caller has already set via setFacialExpression()
    // for the other three channels.
    autoBlinkTimer_ -= dt;
    if (autoBlinkTimer_ <= 0.0f && autoBlinkProgress_ < 0.0f) {
        autoBlinkProgress_ = 0.0f;
        autoBlinkTimer_ = settings_.autoBlinkIntervalSeconds;
    }
    if (autoBlinkProgress_ >= 0.0f) {
        autoBlinkProgress_ += dt;
        float durationFraction = autoBlinkProgress_ / std::max(settings_.autoBlinkDurationSeconds, 0.001f);
        if (durationFraction >= 1.0f) {
            autoBlinkProgress_ = -1.0f;
            targetFacialExpression_.blinkWeight = 0.0f;
        } else {
            targetFacialExpression_.blinkWeight = std::sin(durationFraction * 3.14159265f);
        }
    }

    // Kronos ("Avatar 2.0" -- "Facial System" -- "Ensure facial
    // expressions can be driven by animation curves"): real -- the
    // currently-blended expression (see setFacialExpression()'s own
    // comment for how targetFacialExpression_ is set and blended toward)
    // is applied on top of whatever locomotion pose is already playing,
    // every real tick, so a real gameplay caller can drive blink/smile/
    // frown/talk continuously (e.g. from a Luau script reading a
    // dialogue system's own talk-amplitude) and see it smoothly land on
    // the actual rigged avatar.
    currentFacialExpression_ = blendFacialExpressionTowards(currentFacialExpression_, targetFacialExpression_, dt,
                                                              settings_.facialExpressionBlendSpeed);
    applyFacialExpressionToSkinningMatrices(skinningMatrices, player_.skeleton(), currentFacialExpression_);

    // Kronos ("Avatar 2.0" -- "Accessory Rigging" -- "dynamic offsets,
    // e.g. backpack sway"): real, honest no-op if this character has no
    // Back item equipped at all (the joint still exists, but nothing is
    // bound to it -- swaying an empty joint is harmless). Reuses the
    // same real secondaryMotionPhase_ the head-bob already advances
    // (locomotion-synced), rather than a second, independent phase
    // accumulator for one more small effect.
    applyAccessoryDynamicsToSkinningMatrices(skinningMatrices, player_.skeleton(), secondaryMotionPhase_);

    for (EntityId entity : skinnedEntities) {
        if (auto* t = ecs.tryGetComponent<Transform>(entity)) *t = characterTransform;
        if (auto* skinned = ecs.tryGetComponent<SkinnedRenderable>(entity)) skinned->skinningMatrices = skinningMatrices;
    }
}

} // namespace engine::core
