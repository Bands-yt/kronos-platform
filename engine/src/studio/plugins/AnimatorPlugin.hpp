#pragma once

#include <string>
#include <vector>

#include "core/Animation.hpp"
#include "studio/IStudioPlugin.hpp"

namespace engine::studio::plugins {

// Studio's keyframe animation tool -- the same category of feature as
// Roblox's own built-in Animation Editor and the popular third-party
// keyframe animators the Roblox dev community reaches for (an original
// name/UI/implementation here, deliberately not a copy of either -- see
// README.md's note: the platform's own IP-protection stance
// (safety::IPInfringementScanner) applies to code shipped BY the engine
// too, not just content uploaded TO it, so this ships under a plain
// descriptive name instead of reusing a specific third-party product's).
//
// Workflow: select an entity in Explorer, "Add Track" targets it, scrub
// the timeline or type a playhead time, "Set Key" captures that entity's
// *current* Transform at the playhead into a keyframe. Playing (or just
// dragging the playhead while paused) evaluates every track's pose live
// and writes it into the matching entity's Transform every frame, so the
// Viewport shows the real interpolated pose while you scrub -- not a
// separate preview render.
//
// Clip library: multiple named clips coexist in `library_`, switchable,
// duplicable, deletable. Switching clips while one is already playing
// starts a real crossfade -- both the outgoing and incoming clips keep
// evaluating and advancing during the blend window (crossfadeDuration_),
// per-track lerp/slerp between their two poses, not a hard cut.
//
// Scope cuts, stated plainly: keyframes are placed by scrubbing + Set
// Key, not by dragging an existing keyframe's diamond to retime it;
// tracks target entities by Name, not a skeletal rig (see
// core/Animation.hpp's header note on why); crossfade blends only tracks
// present in *both* clips -- a track unique to one clip snaps in/out at
// the start/end of the blend window rather than fading through some
// implicit rest pose it was never keyframed to have.
class AnimatorPlugin final : public IStudioPlugin {
public:
    [[nodiscard]] const char* name() const override { return "Animator"; }
    [[nodiscard]] const char* category() const override { return "Animation"; }

    void update(float dt, core::ECS& ecs, core::EntityId selected,
                const std::vector<core::EntityId>& selectedEntities) override;
    void drawPanel(core::ECS& ecs, core::EntityId selected,
                   const std::vector<core::EntityId>& selectedEntities) override;

private:
    void drawClipLibraryUi();
    void drawTransport(core::AnimationClip& clip);
    void drawFileControls();
    void drawTrackList(core::ECS& ecs, core::AnimationClip& clip);
    void drawTimeline(const core::AnimationClip& clip);
    void drawCurveEditor(core::AnimationClip& clip);
    // Kronos ("Timeline & Dope Sheet" -- property curves): a real,
    // separate section from drawTrackList()/drawTimeline() above --
    // deliberately not merged into the same graphical timeline canvas.
    // Property tracks target a (targetName, propertyName) pair rather
    // than "the whole Transform," which doesn't fit that canvas's
    // existing one-row-per-Transform-track assumption without a larger
    // rework; a real, separate list keeps this addition bounded and
    // doesn't risk regressing the working Transform UI. See
    // applyPoseToScene()'s own comment for how these get evaluated.
    void drawPropertyTrackList(core::ECS& ecs, core::EntityId selected, core::AnimationClip& clip);
    void applyPoseToScene(core::ECS& ecs) const;
    void startTransitionTo(int newClipIndex);
    [[nodiscard]] static std::string nameOf(core::ECS& ecs, core::EntityId entity);

    std::vector<core::AnimationClip> library_;
    int activeClipIndex_ = -1;

    // Crossfade: both clips keep playing/advancing during the blend --
    // see applyPoseToScene()'s comment.
    int previousClipIndex_ = -1;
    float previousPlayhead_ = 0.0f;
    bool crossfading_ = false;
    float crossfadeElapsed_ = 0.0f;
    float crossfadeDuration_ = 0.3f;

    float playhead_ = 0.0f;
    bool playing_ = false;

    int selectedTrackIndex_ = -1;
    int selectedKeyIndex_ = -1;

    // Timeline zoom/pan/snap -- see drawTimeline()'s comment.
    float timelineZoom_ = 1.0f;
    float timelineScrollSeconds_ = 0.0f;
    bool snapEnabled_ = false;
    float snapIncrementSeconds_ = 0.1f;

    char clipPathBuffer_[256] = "animations/clip.anim";
    std::string statusMessage_;

    // Kronos ("Timeline & Dope Sheet" -- property curves): selection/UI
    // state for the property-track list, kept separate from
    // selectedTrackIndex_/selectedKeyIndex_ above (those are Transform-
    // track-specific) -- same "two real systems, two real selection
    // states" split as the data model itself (core::PropertyTrack vs
    // core::AnimationTrack).
    int selectedPropertyDescriptorIndex_ = 0; // which entry of kRenderableProperties the "Add" combo currently shows
    int selectedPropertyTrackIndex_ = -1;
    int selectedPropertyKeyIndex_ = -1;
};

} // namespace engine::studio::plugins
