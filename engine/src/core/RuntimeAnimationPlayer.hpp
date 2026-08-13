#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/Animation.hpp"
#include "core/ECS.hpp"

namespace engine::core {

// A minimal *runtime* (script-triggered, not Studio-authored) player for
// core::AnimationClip -- the seam Animation.hpp's own header comment
// explicitly reserves for later: "a real runtime AnimationTrack:Play()
// API (per-character, blended, layered) is a genuinely different, larger
// feature that belongs behind the Instance/DataModel Luau surface once
// that exists." That full API (layering, crossfade-on-Play, per-instance
// playback speed) is still not this -- this is the minimum a script
// needs to say "play this clip file now" and have it actually happen:
// load a clip, advance as many of them as are simultaneously active,
// apply each one's tracks to whatever entities in the ECS currently have
// a matching Name (same by-Name targeting AnimationTrack already uses --
// see its header comment on why, and the same accepted "two entities
// sharing a Name both receive the pose" limitation Studio's Animator
// plugin already lives with).
//
// Multiple clips can play concurrently (each play() call is independent,
// not "replaces whatever was playing") -- deliberately simple over
// deliberately complete: no cross-fade between them, no per-clip target
// filtering beyond what each clip's own tracks already specify. A script
// that wants two clips to blend needs Studio's Animator-style crossfade
// logic, which lives in Studio only today (see
// studio/plugins/AnimatorPlugin.hpp) -- not duplicated here.
class RuntimeAnimationPlayer {
public:
    using Handle = uint32_t;
    static constexpr Handle kInvalidHandle = ~0u;

    // Loads `clipPath` (AnimationClip::loadFromFile's format) and starts
    // it playing from time 0. Returns kInvalidHandle if the file doesn't
    // load -- logged, never a silent no-op handle.
    [[nodiscard]] Handle play(const std::string& clipPath, bool looping);
    void stop(Handle handle);

    // Advances every active clip's playhead by dt and applies its tracks'
    // current pose to every Name-matched entity's Transform. Non-looping
    // clips that reach their duration are marked inactive (not erased
    // immediately -- see .cpp) rather than removed mid-iteration.
    void tick(float dt, ECS& ecs);

private:
    struct ActiveClip {
        AnimationClip clip;
        float playhead = 0.0f;
        bool looping = true;
        bool alive = true;
    };

    std::vector<ActiveClip> active_;
};

} // namespace engine::core
