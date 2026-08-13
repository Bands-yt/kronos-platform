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

void AvatarController::tickAnimation(float dt, float horizontalSpeed, bool grounded) {
    if (!grounded) {
        // Jump clip fires once, on the grounded -> airborne edge (not
        // every airborne tick -- replaying it every frame would restart
        // its playhead and never actually finish the animation).
        if (wasGrounded_ && hasJump_) {
            player_.play(jumpClip_, AnimationLayer::Base, /*looping=*/false, settings_.jumpBlendSeconds);
        }
        state_ = AvatarLocomotionState::Jump;
    } else {
        AvatarLocomotionState desired = horizontalSpeed >= settings_.runSpeedThreshold ? AvatarLocomotionState::Run
                                         : horizontalSpeed >= settings_.walkSpeedThreshold ? AvatarLocomotionState::Walk
                                                                                            : AvatarLocomotionState::Idle;
        // Re-trigger locomotion playback on an actual state change, right
        // after landing (state_ == Jump), or on the very first grounded
        // tick ever (!locomotionStarted_ -- see its header comment) --
        // never on every matching tick, or a looping locomotion clip
        // would restart its playhead every frame and never appear to loop.
        if (desired != state_ || state_ == AvatarLocomotionState::Jump || !locomotionStarted_) {
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

    tickAnimation(dt, horizontalSpeed, grounded);

    Transform characterTransform;
    if (auto* t = ecs.tryGetComponent<Transform>(character)) characterTransform = *t;

    const auto& skinningMatrices = player_.skinningMatrices();
    for (EntityId entity : skinnedEntities) {
        if (auto* t = ecs.tryGetComponent<Transform>(entity)) *t = characterTransform;
        if (auto* skinned = ecs.tryGetComponent<SkinnedRenderable>(entity)) skinned->skinningMatrices = skinningMatrices;
    }
}

} // namespace engine::core
