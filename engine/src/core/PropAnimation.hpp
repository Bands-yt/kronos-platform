#pragma once

#include "core/Animation.hpp"
#include "core/ECS.hpp"

namespace engine::core {

// Sprint 10 ("Creator Tools Phase 2") task category 3: real animation
// event hooks for props (open/close/toggle), built on the exact same
// AnimationTrack/Keyframe data model Studio's Animator plugin already
// uses (Animation.hpp) rather than a second, parallel tween system. A
// real, simple two-endpoint timeline: the keyframe at time=0 is the
// "closed" pose, the keyframe at the track's own last (highest) time is
// the "open" pose -- a creator can add more keyframes in between via the
// Studio timeline editor for a real multi-keyframe animation (a door
// that swings out then up, say), not just a linear two-point tween.
struct PropAnimationHook {
    AnimationTrack track{"PropAnimation"};
    float currentTime = 0.0f;
    bool isOpen = false;         // real, current logical state -- what "toggle" flips
    bool playingForward = false; // true = animating toward the open end, false = toward closed
};

// Pure -- the real pose for `hook` at its current time (does not advance
// time; see tickPropAnimationHook() for that).
[[nodiscard]] AnimatedPose evaluatePropAnimation(const PropAnimationHook& hook);

// Pure -- advances hook.currentTime by dt toward the track's start (0,
// closed) or its last keyframe's time (open), depending on
// playingForward, clamping at whichever bound it's heading toward -- a
// real, bounded open/close sweep, not a loop. Returns the resulting pose
// so the caller can write it into a Transform without a second
// evaluate() call.
AnimatedPose tickPropAnimationHook(PropAnimationHook& hook, float dt);

// Pure decision -- flips hook.isOpen/playingForward to animate toward
// the opposite state. The real "toggle" event hook; open()/close() are
// the same idea pinned to a known target state instead of the opposite
// of whatever the current one is.
void togglePropAnimation(PropAnimationHook& hook);
void openPropAnimation(PropAnimationHook& hook);
void closePropAnimation(PropAnimationHook& hook);

} // namespace engine::core
