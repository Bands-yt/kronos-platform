#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "core/Animation.hpp"
#include "core/Skeleton.hpp"

namespace engine::core {

// Linear = AnimationTrack::evaluate() (straight-line lerp/slerp between
// the bracketing pair). Cubic = AnimationTrack::evaluateCubic() (Catmull-
// Rom through position/scale, slerp for rotation -- see that method's
// header comment). A player-wide setting rather than per-clip: nothing in
// this codebase's content needs to mix interpolation modes within one
// character today.
enum class AnimationInterpolation { Linear, Cubic };

[[nodiscard]] const char* animationInterpolationName(AnimationInterpolation mode);

// Which of a fixed two layers a clip plays on -- exactly the "base +
// upper body" split the task asks for, not a general N-layer stack (no
// real content in this engine needs a third layer yet, and a fixed
// two-entry array is simpler and just as testable as a dynamic one for
// what's actually here). Base normally drives the whole skeleton (idle/
// walk/run, full-body emotes); UpperBody optionally overrides whatever
// joints its own clips have tracks for (e.g. a wave emote layered on top
// of a walk cycle) -- see AnimationPlayer::tick()'s pose-composition
// comment for exactly how the two combine per joint.
enum class AnimationLayer { Base, UpperBody };
constexpr size_t kAnimationLayerCount = 2;

// A real skeletal animation player -- distinct from RuntimeAnimationPlayer
// (which targets ECS entities' Transform by Name, see its header comment
// for why that one exists separately). This one drives a single
// core::Skeleton's joints from one or more concurrently-blending
// core::AnimationClip instances (whose tracks target joint names instead
// of entity names -- the same AnimationClip/AnimationTrack data model,
// just pointed at a different kind of "thing with a Name"), and produces
// the per-joint skinning matrices core::SkinnedRenderable needs every
// frame.
//
// One AnimationPlayer owns exactly one Skeleton and drives exactly one
// rigged character's pose -- the same "one active thing per owner" scope
// RuntimeAnimationPlayer's own header comment already accepts (multiple
// concurrent *characters* means multiple AnimationPlayer instances, one
// per core::AvatarController -- see that class).
class AnimationPlayer {
public:
    using Handle = uint32_t;
    static constexpr Handle kInvalidHandle = ~0u;

    explicit AnimationPlayer(Skeleton skeleton);

    [[nodiscard]] const Skeleton& skeleton() const { return skeleton_; }

    void setInterpolation(AnimationInterpolation mode) { interpolation_ = mode; }
    [[nodiscard]] AnimationInterpolation interpolation() const { return interpolation_; }

    // Starts `clip` playing on `layer`. If `fadeSeconds` > 0, every
    // currently-alive clip already on that layer crossfades out (weight
    // ramps from wherever it currently is down to 0, over `fadeSeconds`,
    // then is removed) while this new clip crossfades in (0 -> 1 over the
    // same duration) -- a real, interruptible blend: calling play() again
    // mid-fade re-fades whatever was fading in from its *current* weight,
    // not from 1, so rapid re-triggers never pop. fadeSeconds <= 0 is an
    // instant cut (every other clip on the layer is dropped immediately).
    // Returns a handle identifying this specific playback instance, valid
    // until it's stop()'d or naturally faded out.
    Handle play(AnimationClip clip, AnimationLayer layer, bool looping, float fadeSeconds = 0.0f);

    // Fades `handle`'s weight to 0 over `fadeSeconds` (0 = remove
    // immediately) and removes it once settled. A no-op if `handle` isn't
    // currently active (already stopped/faded out/never existed).
    void stop(Handle handle, float fadeSeconds = 0.0f);

    void pause(Handle handle);
    void resume(Handle handle);
    [[nodiscard]] bool isPlaying(Handle handle) const;

    // Jumps `handle`'s playhead directly to `time` (clamped to
    // [0, clip.duration]) -- what a Studio timeline scrubber (see
    // studio/plugins/AnimationPreviewerPlugin.hpp) drags against. Does not
    // affect fade weight.
    void seek(Handle handle, float time);
    [[nodiscard]] float playhead(Handle handle) const;

    // Advances every active clip's playhead + fade weight by dt, then
    // recomputes this tick's pose (see the .cpp's header comment on
    // exactly how Base/UpperBody combine per joint) and skinningMatrices().
    // Also collects any AnimationEvent whose time was crossed this tick
    // (on a clip whose current blend weight is audible, i.e. > ~0) into an
    // internal queue -- see consumeFiredEvents(). Assumes dt is a normal
    // per-frame delta, much smaller than any playing clip's duration (the
    // same assumption RuntimeAnimationPlayer::tick() already makes) -- a
    // single call advancing across more than one full loop of a looping
    // clip may miss events from the skipped-over wrap(s).
    void tick(float dt);

    // This tick's per-joint skinning matrices (currentJointWorld *
    // skeleton().inverseBindMatrices()[joint]) -- hand straight to
    // SkinnedRenderable::skinningMatrices (see Components.hpp).
    [[nodiscard]] const std::vector<glm::mat4>& skinningMatrices() const { return skinningMatrices_; }

    // Events crossed during the most recent tick() call, across every
    // active clip on every layer. Draining, not peeking: calling this
    // clears the queue, so each firing reaches exactly one caller.
    [[nodiscard]] std::vector<std::string> consumeFiredEvents();

private:
    struct ActiveClip {
        Handle handle = kInvalidHandle;
        AnimationClip clip;
        float playheadTime = 0.0f;
        bool looping = true;
        bool paused = false;
        bool alive = true;

        float weight = 1.0f;
        float fadeStartWeight = 1.0f;
        float fadeTargetWeight = 1.0f;
        float fadeDuration = 0.0f;
        float fadeElapsed = 0.0f;
    };

    [[nodiscard]] ActiveClip* findActive(Handle handle);
    [[nodiscard]] const ActiveClip* findActive(Handle handle) const;
    void startFade(ActiveClip& active, float targetWeight, float fadeSeconds);
    void tickLayer(std::vector<ActiveClip>& clips, float dt);
    // Blends `jointName`'s pose across every audible clip in `clips` --
    // outPose is only meaningful if this returns true (nothing in `clips`
    // has a track for this joint otherwise). outWeightSum is the summed
    // blend weight of every contributing clip, used by tick() to know how
    // strongly the UpperBody layer should override Base for this joint.
    [[nodiscard]] bool evaluateLayerPoseForJoint(const std::vector<ActiveClip>& clips, const std::string& jointName,
                                                  AnimatedPose& outPose, float& outWeightSum) const;

    Skeleton skeleton_;
    std::vector<glm::mat4> inverseBindMatrices_;
    std::array<std::vector<ActiveClip>, kAnimationLayerCount> layers_;
    std::vector<glm::mat4> skinningMatrices_;
    std::vector<std::string> firedEvents_;
    AnimationInterpolation interpolation_ = AnimationInterpolation::Linear;
    Handle nextHandle_ = 0;
};

} // namespace engine::core
