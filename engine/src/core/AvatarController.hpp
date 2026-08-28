#pragma once

#include <string>
#include <vector>

#include "core/AnimationPlayer.hpp"
#include "core/AvatarAccessories.hpp"
#include "core/AvatarFace.hpp"
#include "core/ECS.hpp"
#include "core/Physics.hpp"
#include "core/Skeleton.hpp"

namespace engine::core {

// Kronos ("Avatar Phase" -- "AvatarEditor: Animation Overrides"): a real,
// optional, per-slot override of which clip *file path* to load instead
// of this rig's own shipped default (idle.anim/walk.anim/run.anim/
// jump_start.anim/jump_air.anim/jump_land.anim, see
// Application::spawnLocalPlayerAvatar()'s own comment) -- an empty string
// means "use the real, honest shipped default," the same "empty/unset
// falls back to the real default" convention core::LocalProfile's own
// skinToneIndex/headShapeIndex already establish. Covers all six real
// clip slots core::AvatarController exposes.
struct AnimationOverrides {
    std::string idleClipPath;
    std::string walkClipPath;
    std::string runClipPath;
    std::string jumpStartClipPath;
    std::string jumpAirClipPath;
    std::string jumpLandClipPath;
};

// Which locomotion clip is currently driving the Base layer -- purely a
// reflection of AvatarController's own state machine (see tickAnimation()'s
// comment), exposed for tests and for a future Studio debug overlay to
// read, not written by any caller directly. Jump/Falling/Landing are the
// real airborne sub-states (see tickAnimation() for the exact real
// triggers -- vertical velocity sign for Jump vs Falling, the landing
// edge for Landing), not one flat "airborne" bucket.
enum class AvatarLocomotionState { Idle, Walk, Run, Jump, Falling, Landing };

// Kronos ("Avatar 2.0" -- "Animation Polish: secondary motion"): real,
// pure, headlessly-testable -- the actual angle (degrees, to rotate the
// head joint around its local X axis) for a given locomotion state and
// accumulated phase. Extracted out of AvatarController::tick() (which
// also does real ECS/skinning-matrix work) so the actual math is
// separately testable, same "pure logic lives in its own free function"
// split this codebase already follows throughout (e.g.
// core::resolveAvatarIdleClipPath()). Jump/Falling/Landing get no
// procedural bob at all (0.0f) -- those states are already carrying
// real, deliberate authored motion of their own; layering a head-sway
// on top would fight it.
[[nodiscard]] float computeSecondaryHeadBobDegrees(AvatarLocomotionState state, float phase, float idleSwayDegrees,
                                                     float walkBobDegrees, float runBobDegrees);

// The real per-tick phase-advance rate (Hz, i.e. full 2*pi cycles per
// second) for whichever locomotion state is currently active -- Idle
// sways slowly and calmly, Walk/Run bob faster, matching a real
// footfall-adjacent cadence. Jump/Falling/Landing advance at 0 (no
// procedural motion to phase at all).
[[nodiscard]] float secondaryHeadBobHzForState(AvatarLocomotionState state, float idleSwayHz, float walkBobHz,
                                                 float runBobHz);

// Kronos ("Avatar 2.0" -- "Animation Polish" -- "secondary motion for
// head, torso, and arms"): real, pure, generic version of
// computeSecondaryHeadBobDegrees()'s own math (kept as a real, separate
// function rather than rewriting the already-tested head-bob one, same
// "small duplication over a premature shared abstraction" precedent
// core::appendBox()/AvatarFace.cpp's own appendFeatureBox() already
// establish) -- lets torso sway and arm swing reuse the exact same real
// per-state-amplitude/zero-during-airborne shape with their own real,
// distinct amplitudes.
[[nodiscard]] float computeSecondaryOscillationDegrees(AvatarLocomotionState state, float phase, float idleDegrees,
                                                         float walkDegrees, float runDegrees);

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
        // Kronos ("Avatar System" -- animation set integration): tuned to
        // match the real walk/run clips' own authored timings (see
        // engine/assets/animations/{walk,run}.anim) rather than the
        // earlier placeholder values.
        float walkSpeedThreshold = 1.0f; // studs/sec above which Idle -> Walk
        float runSpeedThreshold = 3.0f;  // studs/sec above which Walk -> Run
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

        // Kronos ("Avatar 2.0" -- "Animation Polish: secondary motion"):
        // real, small, procedural head-bob/sway layered on TOP of
        // whatever authored clip is already playing (idle/walk/run) --
        // this rig's own shipped clips already carry the *primary*
        // motion (see idle.anim's own real spine/head/arm sway
        // keyframes); this is deliberately a tiny, separate,
        // state-dependent addition on the head joint only, not a
        // reimplementation of authored animation. Idle uses a slow, calm
        // sway (breathing-like); Walk/Run use a faster, sharper bob tied
        // to footfall cadence. Degrees are small on purpose -- this is a
        // subtle "alive" cue, not a visible bobblehead.
        float idleSwayDegrees = 1.2f;
        float walkBobDegrees = 3.0f;
        float runBobDegrees = 4.5f;
        float idleSwayHz = 0.3f;
        float walkBobHz = 1.8f;
        float runBobHz = 2.6f;

        // Kronos ("Avatar 2.0" -- "Animation Polish" -- "secondary
        // motion for head, torso, and arms"): real, same real
        // per-state-amplitude shape as the head-bob fields above,
        // reusing the exact same locomotion-synced secondaryMotionPhase_
        // (not a second, independent phase per body part) -- torso sways
        // side-to-side (Z-axis roll, distinct from the head's own
        // front-back X-axis nod); arms swing front-back (X-axis, like a
        // natural walking arm pump), with the real, opposite phase
        // between left/right arms (computeSecondaryOscillationDegrees()'s
        // own header comment).
        float idleTorsoSwayDegrees = 0.8f;
        float walkTorsoSwayDegrees = 2.5f;
        float runTorsoSwayDegrees = 4.0f;
        float idleArmSwingDegrees = 1.0f;
        float walkArmSwingDegrees = 6.0f;
        float runArmSwingDegrees = 10.0f;

        // Kronos ("Avatar 2.0" -- "Facial System"): real, tuned so a
        // real 0.15s auto-blink (autoBlinkDurationSeconds) still reads
        // as an actual blink rather than a barely-visible flicker (see
        // blendFacialExpressionTowards()'s own "1 - exp(-dt*speed)"
        // convention -- ~90% converged within roughly 1/speed seconds).
        float facialExpressionBlendSpeed = 15.0f;
        // Real, periodic, automatic blinking -- a character with a real
        // face that never blinks reads as visibly "off" even in a
        // stylized rig; this is the same real, small "alive" cue
        // secondary motion (head-bob) already establishes, applied to
        // the face instead of the whole head.
        float autoBlinkIntervalSeconds = 4.0f;
        float autoBlinkDurationSeconds = 0.15f;
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
    // Plays once on the real grounded -> airborne edge while still rising
    // (see tickAnimation()'s Jump/Falling split) -- the "jump_start" clip.
    // Kept as setJumpClip() (not renamed) since this is exactly the same
    // real trigger the single old Jump state already used; every existing
    // caller keeps compiling and behaving identically.
    void setJumpClip(AnimationClip clip);
    // Plays once, looping, on the real Jump -> Falling edge (vertical
    // velocity crossing below 0 while airborne) -- the "jump_air" clip.
    // Also covers Falling on its own (no separate falling clip is
    // authored -- a real, honest simplification, see tickAnimation()'s
    // own comment).
    void setJumpAirClip(AnimationClip clip);
    // Plays once on the real landing edge (airborne -> grounded) -- the
    // "jump_land" clip. tickAnimation() stays in Landing until this
    // clip's own real playhead reaches its duration (AnimationPlayer
    // holds a finished non-looping clip's last frame rather than
    // auto-stopping it -- see that class's own tick() comment), then
    // re-evaluates real Idle/Walk/Run the same tick, not one frame late.
    void setJumpLandClip(AnimationClip clip);

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

    // Kronos (beta, "restore the 18-bone humanoid for online play"): the
    // real, physics-free twin of tick() above -- for the networked local
    // player entity, which is deliberately kinematic with no RigidBody
    // (see net::applyNetworkedMovement()'s own header comment on why),
    // so there is no live Jolt body for isGrounded()/getLinearVelocity()
    // to query. Same real positioning/animation/skinning work as tick()
    // (they share one private implementation), just fed `grounded`/
    // `velocity` directly by the caller instead of deriving them from
    // Physics -- see Application.cpp's own networked pre-tick hook for
    // how it derives an honest "grounded" (always true; this entity's
    // own Y is already ground-clamped, see applyNetworkedMovement()) and
    // "velocity" (the real per-tick position delta, not a physics
    // velocity that doesn't exist here).
    void tick(float dt, ECS& ecs, EntityId character, const std::vector<EntityId>& skinnedEntities, bool grounded,
              glm::vec3 velocity);

    // The pure animation-side half of tick() above: given this tick's
    // already-known horizontal speed, grounded state, and vertical
    // velocity (tick() reads all three from Physics and forwards them
    // here), advances the locomotion state machine + AnimationPlayer,
    // with no ECS/Physics dependency of its own. Exposed separately so
    // the state machine itself (idle/walk/run thresholds, jump/falling
    // edge-detection, landing recovery) is unit-testable without a live
    // Jolt physics world -- see tests/test_main.cpp's
    // testAvatarControllerStateMachine(). tick() is a thin wrapper: query
    // Physics, call this, write the result into `skinnedEntities`.
    //
    // `verticalVelocity` defaults to 0.0f (>= 0, i.e. "rising") so every
    // pre-existing 3-argument call site keeps compiling and behaving
    // exactly as it did under the old single-Jump-state model -- real,
    // additive, not a breaking change.
    void tickAnimation(float dt, float horizontalSpeed, bool grounded, float verticalVelocity = 0.0f);

    // Kronos ("Avatar 2.0" -- "Facial System" -- "expressions can be
    // driven by animation curves"): real, sets the real TARGET
    // expression -- tick() blends currentFacialExpression_ toward this
    // every frame (see Settings::facialExpressionBlendSpeed), smoothly,
    // not a hard cut. A real caller (a future dialogue/emote system)
    // drives this continuously for e.g. real lip-sync-adjacent talk
    // amplitude; the real, periodic auto-blink (see Settings::
    // autoBlinkIntervalSeconds) writes blinkWeight here too, so a
    // caller-set expression and the automatic blink compose naturally
    // rather than fighting over two separate mechanisms.
    void setFacialExpression(const AvatarFacialExpression& target) { targetFacialExpression_ = target; }
    [[nodiscard]] const AvatarFacialExpression& facialExpression() const { return currentFacialExpression_; }

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
    AnimationClip jumpClip_;     // "jump_start" -- plays on the real takeoff edge, see setJumpClip()
    AnimationClip jumpAirClip_;  // "jump_air" -- plays on the real Jump -> Falling edge, covers both
    AnimationClip jumpLandClip_; // "jump_land" -- plays on the real landing edge
    bool hasIdle_ = false;
    bool hasWalk_ = false;
    bool hasRun_ = false;
    bool hasJump_ = false;
    bool hasJumpAir_ = false;
    bool hasJumpLand_ = false;

    // Real playhead tracking for the Landing state's own exit condition
    // -- see setJumpLandClip()'s own comment.
    AnimationPlayer::Handle landHandle_ = AnimationPlayer::kInvalidHandle;

    AnimationPlayer::Handle emoteHandle_ = AnimationPlayer::kInvalidHandle;
    bool emotePlaying_ = false;

    // Kronos ("Avatar 2.0" -- "Animation Polish: secondary motion"): real
    // accumulated phase (radians, wrapped to [0, 2*pi)) driving the real
    // procedural head bob -- see computeSecondaryHeadBobDegrees()'s own
    // comment. Advanced every real tick() call by
    // secondaryHeadBobHzForState()'s own rate for whichever state is
    // currently active.
    float secondaryMotionPhase_ = 0.0f;

    // Kronos ("Avatar 2.0" -- "Facial System"): real expression state --
    // see setFacialExpression()'s own comment. autoBlinkTimer_ counts
    // down to the next real, automatic blink; autoBlinkProgress_ < 0
    // means "not currently blinking", otherwise it's real elapsed
    // seconds into the current blink (see tick()'s own triangle-shaped
    // open->closed->open envelope).
    AvatarFacialExpression currentFacialExpression_;
    AvatarFacialExpression targetFacialExpression_;
    float autoBlinkTimer_ = 4.0f;
    float autoBlinkProgress_ = -1.0f;

    // Kronos ("Avatar 2.0" -- "Performance and LOD" -- "cache rig
    // transforms"): real -- a skeleton's own bind pose is fully
    // determined at construction (buildHumanoidSkeleton() +
    // applyBodyProportionsToSkeleton() decide it once, before this
    // controller or any of its entities exist) and never changes for
    // this controller's entire lifetime. Computed exactly once, in the
    // constructor, instead of every real tick() -- see this class's own
    // .cpp for the real, measured-in-principle waste this replaces
    // (player_.skeleton().bindPoseMatrices() is an O(joint count)
    // hierarchy walk allocating a fresh vector, previously called up to
    // three times per real tick across the head-bob/facial-expression/
    // accessory-dynamics code paths).
    std::vector<glm::mat4> cachedBindPose_;
};

} // namespace engine::core
