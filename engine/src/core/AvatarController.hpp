#pragma once

#include <vector>

#include "core/AnimationPlayer.hpp"
#include "core/ECS.hpp"
#include "core/Physics.hpp"
#include "core/Skeleton.hpp"

namespace engine::core {

// Which locomotion clip is currently driving the Base layer -- purely a
// reflection of AvatarController's own state machine (see tick()'s
// comment), exposed for tests and for a future Studio debug overlay to
// read, not written by any caller directly.
enum class AvatarLocomotionState { Idle, Walk, Run, Jump };

// The animation-side counterpart to CharacterController -- that class
// owns input/physics/camera (see its own header comment); this one owns
// picking and blending the right clips for however CharacterController
// is actually moving this tick, and writing the resulting pose into a
// rigged avatar's SkinnedRenderable entities (see RiggedAvatar.hpp's
// spawnRiggedAvatar()). Deliberately does NOT reimplement movement or
// camera-follow -- it reads `character`'s already-simulated velocity/
// grounded state and Transform (see tick()'s parameters) the same tick
// CharacterController::tick() runs, the same "physics/camera owned
// elsewhere, animation reads the result" split RuntimeAnimationPlayer
// already has relative to Physics for plain (non-skeletal) Transform
// animation.
class AvatarController {
public:
    struct Settings {
        float walkSpeedThreshold = 0.5f; // studs/sec above which Idle -> Walk
        float runSpeedThreshold = 6.0f;  // studs/sec above which Walk -> Run
        float locomotionBlendSeconds = 0.25f;
        float jumpBlendSeconds = 0.1f;
        float emoteBlendSeconds = 0.15f;
        // Must match whatever CharacterController::Settings the physics
        // capsule was actually spawned with -- AvatarController doesn't
        // hold a reference to CharacterController (staying decoupled from
        // its input/camera concerns), so it needs its own copy of just
        // the two dimensions isGrounded()'s raycast needs.
        float capsuleRadius = 0.35f;
        float capsuleHalfHeight = 0.55f;
    };

    // Two constructors rather than one with `Settings settings = {}` --
    // the same nested-class-default-member-initializer-via-default-
    // argument restriction CharacterController::CharacterController()'s
    // own header comment documents; split the same way for the same
    // reason.
    explicit AvatarController(Skeleton skeleton);
    AvatarController(Skeleton skeleton, Settings settings);

    void setIdleClip(AnimationClip clip);
    void setWalkClip(AnimationClip clip);
    void setRunClip(AnimationClip clip);
    void setJumpClip(AnimationClip clip);

    // Starts `clip` playing as an emote -- on the UpperBody layer (blends
    // over whatever locomotion is already playing, e.g. a wave while
    // walking) unless `fullBody` is true, in which case it plays on the
    // Base layer and *replaces* locomotion entirely (a seated/dance
    // emote), automatically ceding back to idle/walk/run the moment
    // tick() next observes a locomotion-state change (e.g. the player
    // starts moving) -- no special-casing needed for that handoff, see
    // tick()'s comment for why.
    void playEmote(AnimationClip clip, bool looping, bool fullBody = false);
    void stopEmote();
    [[nodiscard]] bool isEmotePlaying() const;

    // Reads `character`'s current grounded state (via `physics`) and
    // world Transform, drives the idle/walk/run/jump blend tree and
    // AnimationPlayer forward by dt (via tickAnimation() below), then
    // writes the resulting pose (skinningMatrices) and the character's
    // current world Transform into every entity in `skinnedEntities` (all
    // sharing this controller's one Skeleton, typically
    // RiggedAvatar.hpp's spawnRiggedAvatar() output). Call this after
    // CharacterController::tick() (which sets `character`'s velocity/
    // rotation for this tick) but the ordering relative to Physics::step()
    // doesn't matter for this class specifically -- it only reads
    // whatever Physics::step() computed *last* tick via
    // getLinearVelocity()/isGrounded(), same as any other post-physics
    // read.
    void tick(float dt, ECS& ecs, Physics& physics, EntityId character, const std::vector<EntityId>& skinnedEntities);

    // The pure animation-side half of tick() above: given this tick's
    // already-known horizontal speed and grounded state (tick() reads
    // both from Physics and forwards them here), advances the locomotion
    // state machine + AnimationPlayer, with no ECS/Physics dependency of
    // its own. Exposed separately so the state machine itself
    // (idle/walk/run thresholds, jump edge-detection, landing recovery)
    // is unit-testable without a live Jolt physics world -- see
    // tests/test_main.cpp's testAvatarControllerStateMachine(). tick()
    // is a thin wrapper: query Physics, call this, write the result into
    // `skinnedEntities`.
    void tickAnimation(float dt, float horizontalSpeed, bool grounded);

    [[nodiscard]] AvatarLocomotionState locomotionState() const { return state_; }
    [[nodiscard]] const AnimationPlayer& animationPlayer() const { return player_; }
    [[nodiscard]] const Skeleton& skeleton() const { return player_.skeleton(); }

private:
    AnimationPlayer player_;
    Settings settings_;

    AvatarLocomotionState state_ = AvatarLocomotionState::Idle;
    bool wasGrounded_ = true;
    // The state-change guard in tickAnimation() only re-triggers play()
    // when `desired` differs from `state_` -- which would otherwise skip
    // the very first tick ever (both are Idle before anything has
    // played), leaving the character posed at bind-pose with nothing
    // actually playing. This flag forces exactly one unconditional
    // trigger on that first grounded tick.
    bool locomotionStarted_ = false;

    AnimationClip idleClip_;
    AnimationClip walkClip_;
    AnimationClip runClip_;
    AnimationClip jumpClip_;
    bool hasIdle_ = false;
    bool hasWalk_ = false;
    bool hasRun_ = false;
    bool hasJump_ = false;

    AnimationPlayer::Handle emoteHandle_ = AnimationPlayer::kInvalidHandle;
    bool emotePlaying_ = false;
};

} // namespace engine::core
