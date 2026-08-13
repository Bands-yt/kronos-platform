#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace engine::core {

class Skeleton;

// Keyframe-based Transform animation -- the data model behind Studio's
// Animator plugin (studio/plugins/AnimatorPlugin.hpp), in the same spirit
// as Roblox's own Animation Editor and popular third-party keyframe tools:
// pose an entity at a handful of times, interpolate everything between.
//
// Scope note: tracks target entities *by Name* (AnimationTrack::targetName),
// not by core::EntityId. EntityId (an entt::entity) is a runtime handle
// with no persistence across sessions -- nothing in this codebase assigns
// entities a stable GUID yet (see docs/ARCHITECTURE.md §6's still-missing
// Instance/DataModel layer). Name is the only identifier that survives a
// save/load round trip today, so that's what a clip stores. Two entities
// sharing a Name will both receive the same pose -- an accepted limitation
// until a real persistent-id system exists, not a bug.
//
// Playback lives here, not in a separate "AnimationPlayer" ECS component
// or system: nothing in this codebase yet needs more than one active clip
// at a time (Studio's Animator plugin owns exactly one), so a system pass
// over multiple simultaneous players would be speculative machinery ahead
// of an actual second caller. AnimatorPlugin owns an AnimationClip and
// drives evaluate() itself every frame; a real runtime AnimationTrack:Play()
// API (per-character, blended, layered) is a genuinely different, larger
// feature that belongs behind the Instance/DataModel Luau surface once that
// exists, not bolted on here ahead of time.

enum class EasingMode {
    Linear,
    EaseIn,
    EaseOut,
    EaseInOut,
    Constant, // holds this keyframe's pose until the next keyframe's time, then jumps -- no interpolation
};

[[nodiscard]] const char* easingModeName(EasingMode mode);

// Applies `mode` to a normalized segment parameter t in [0, 1]. Constant
// is handled by the caller (AnimationTrack::evaluate), not here, since it
// changes which *values* get blended, not just the blend curve.
[[nodiscard]] float applyEasing(EasingMode mode, float t);

struct Keyframe {
    float time = 0.0f; // seconds from clip start
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};
    // Easing applied to the segment starting AT this keyframe, i.e.
    // interpolating from this keyframe to the next one uses this mode --
    // matches the convention most keyframe animators use (ease is a
    // property of the outgoing segment, not the incoming one).
    EasingMode easing = EasingMode::Linear;
};

struct AnimatedPose {
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};
};

// A time-tagged marker on a clip -- footstep sounds, emote loop points,
// VFX cues. `name` is a single token (no embedded whitespace, same
// convention as Joint::name/AnimationTrack::targetName -- see
// AnimationClip::saveToFile's grammar, which reads it with `iss >>`).
// Pure data: firing/consuming events is core::AnimationPlayer's job (see
// AnimationPlayer.hpp), not this file's -- AnimationClip only carries the
// authored marker, the same "data model vs playback" split
// AnimationTrack/AnimationClip already have with RuntimeAnimationPlayer.
struct AnimationEvent {
    float time = 0.0f;
    std::string name;
};

class AnimationTrack {
public:
    explicit AnimationTrack(std::string targetName) : targetName_(std::move(targetName)) {}

    [[nodiscard]] const std::string& targetName() const { return targetName_; }
    [[nodiscard]] const std::vector<Keyframe>& keyframes() const { return keyframes_; }

    // Inserts in time order. If an existing keyframe is within 1ms of
    // `keyframe.time`, it's replaced (re-keying at the same time moves the
    // pose, it doesn't create a duplicate) rather than appended.
    void addKeyframe(Keyframe keyframe);
    void removeKeyframeAt(size_t index);

    // Mutable access for in-place field edits (easing mode, pose values)
    // that don't change `.time` -- changing time here would break the
    // sorted invariant addKeyframe()/evaluate() depend on, since nothing
    // re-sorts after this call. No UI path does that today (Studio's
    // Animator plugin only offers Combo-box easing edits through this);
    // if a future drag-to-retime feature needs it, re-sort after use.
    [[nodiscard]] Keyframe& keyframeAt(size_t index) { return keyframes_[index]; }

    // Real interpolation, not a stub: position/scale are lerp'd,
    // rotation is slerp'd (via glm::slerp, shortest-path), easing is
    // applied to the segment's normalized time before blending. Clamped
    // to the first/last keyframe's pose outside the track's own time
    // range. Returns the identity pose if the track has no keyframes.
    [[nodiscard]] AnimatedPose evaluate(float time) const;

    // Real Catmull-Rom cubic interpolation through position/scale (a
    // smooth curve through every keyframe rather than evaluate()'s
    // straight-line segments, using the keyframe before `a` and after `b`
    // as extra control points -- clamped/duplicated at the clip's own
    // boundaries, the standard Catmull-Rom edge handling). Rotation is
    // still slerp'd between the bracketing pair, not a true spherical
    // cubic (squad) -- a real, deliberately un-built extension (squad
    // needs each keyframe's own quaternion "tangent," a meaningfully
    // larger feature); slerp already gives a shortest-path, non-jerky
    // rotation, just without the extra smoothing through neighboring
    // keyframes position/scale get here. Falls back to evaluate() outright
    // when there are fewer than 3 keyframes (a cubic segment needs a
    // neighbor on at least one side to mean anything beyond a straight
    // line, so with 1-2 keyframes there's nothing cubic to compute).
    // Respects Constant easing exactly like evaluate() (holds `a`'s pose).
    [[nodiscard]] AnimatedPose evaluateCubic(float time) const;

private:
    std::string targetName_;
    std::vector<Keyframe> keyframes_; // kept sorted ascending by time
};

class AnimationClip {
public:
    std::string name = "New Animation";
    float duration = 5.0f;
    bool looping = true;
    std::vector<AnimationTrack> tracks;
    std::vector<AnimationEvent> events; // unordered -- AnimationPlayer scans all of them each tick, see its header comment

    // Finds the track for `targetName`, creating an empty one if none
    // exists yet -- the "add a track by selecting an entity and setting a
    // key" workflow AnimatorPlugin drives. Not [[nodiscard]]: calling this
    // purely for the create-if-missing side effect (Animator's "Add
    // Track" button) is a legitimate, intentional use, not a bug.
    AnimationTrack& trackFor(const std::string& targetName);
    void removeTrack(const std::string& targetName);

    // Minimal hand-rolled text format (see Animation.cpp for the exact
    // grammar) -- no JSON/serialization library pulled in for a data shape
    // this small. Real asset-pipeline persistence (AssetConverter's
    // Animation path, still a documented stub) would replace this with
    // whatever real clip format the engine standardizes on; this is
    // Studio-local save/load so the Animator plugin is actually usable
    // across sessions today, not a claim about the final on-disk format.
    [[nodiscard]] bool saveToFile(const std::string& path) const;
    [[nodiscard]] bool loadFromFile(const std::string& path);
};

// Every keyframe time used by any track in `clip`, deduplicated (within
// 1ms, the same kKeyframeMergeEpsilon addKeyframe() itself re-keys at)
// and sorted ascending -- what a timeline UI (studio::plugins::
// AnimationPreviewerPlugin's scrubber) draws tick marks at. Pure clip
// analysis, not UI-specific, so it lives here next to AnimationClip
// itself rather than in a plugin's own translation unit.
[[nodiscard]] std::vector<float> collectKeyframeTimes(const AnimationClip& clip);

// Real, structural validation of a clip *against a specific skeleton* --
// distinct from (and a layer above) AnimationItem::validate(), which only
// checks the manifest's own fields (id/name/clipPath existing) and has no
// Skeleton to check against. Two real problems this catches, both
// task-named ("joint mismatch," "missing channels"):
//  - a track whose targetName() doesn't resolve to any joint in
//    `skeleton` ("joint mismatch" -- the clip was authored against a
//    different/incompatible rig, or a joint name was typo'd).
//  - the clip having no tracks at all, or a track with zero keyframes
//    ("missing channels" -- nothing would actually animate).
// Returns false and fills `outError` with the first problem found.
[[nodiscard]] bool validateAnimationClipAgainstSkeleton(const AnimationClip& clip, const Skeleton& skeleton,
                                                          std::string& outError);

} // namespace engine::core
