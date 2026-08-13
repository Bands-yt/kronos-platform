#include "core/PropAnimation.hpp"

#include <algorithm>

namespace engine::core {

AnimatedPose evaluatePropAnimation(const PropAnimationHook& hook) { return hook.track.evaluate(hook.currentTime); }

AnimatedPose tickPropAnimationHook(PropAnimationHook& hook, float dt) {
    float targetTime = 0.0f;
    if (hook.playingForward && !hook.track.keyframes().empty()) {
        targetTime = hook.track.keyframes().back().time;
    }

    if (hook.currentTime < targetTime) {
        hook.currentTime = std::min(hook.currentTime + dt, targetTime);
    } else if (hook.currentTime > targetTime) {
        hook.currentTime = std::max(hook.currentTime - dt, targetTime);
    }

    return evaluatePropAnimation(hook);
}

void togglePropAnimation(PropAnimationHook& hook) {
    hook.isOpen = !hook.isOpen;
    hook.playingForward = hook.isOpen;
}

void openPropAnimation(PropAnimationHook& hook) {
    hook.isOpen = true;
    hook.playingForward = true;
}

void closePropAnimation(PropAnimationHook& hook) {
    hook.isOpen = false;
    hook.playingForward = false;
}

} // namespace engine::core
