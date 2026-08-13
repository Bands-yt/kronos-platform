#pragma once

#include <string>
#include <vector>

namespace engine::core {

// A creator-uploaded clip's catalogue category -- deliberately just three,
// per the task's own scope ("category: emote/locomotion/misc"), not
// AvatarItemCategory's eight equip slots (an animation clip doesn't equip
// onto a body part, it plays). Emote is the one category
// AvatarLoadout/the Emote System (see EmoteSystem.hpp) actually filters
// on; Locomotion/Misc exist so idle/walk/run/jump content uploaded through
// this same pipeline has a real, honest home instead of being force-fit
// into "Emote".
enum class AnimationCategory { Emote, Locomotion, Misc };

[[nodiscard]] const char* animationCategoryName(AnimationCategory category);
[[nodiscard]] bool animationCategoryFromName(const std::string& name, AnimationCategory& out);

// One catalogue clip's real content description -- what .anim file to
// load and the display metadata a browsing UI needs without loading it.
// Deliberately does NOT embed the clip itself (unlike AvatarItem, which
// has no separate "asset file" concept for its material) -- an
// AnimationClip is real, potentially large keyframe data with its own
// established save/load format (Animation.hpp), so this just points at
// it, the same "manifest points at real files" split
// core::AvatarItem::meshPath/texturePath already use.
struct AnimationItem {
    std::string id;
    std::string name;
    AnimationCategory category = AnimationCategory::Misc;
    std::vector<std::string> tags;

    std::string clipPath; // real filesystem path to a .anim file (core::AnimationClip::loadFromFile)

    // Cached metadata mirrors of AnimationClip::looping/duration -- a
    // catalogue browsing UI (studio::CataloguePanel-equivalent for
    // animations, or a future Emote System list) can filter/display
    // these without loading and parsing the full clip file for every
    // entry, the same reasoning core::AssetMetadata caches image
    // dimensions instead of re-decoding every thumbnail. Set from the
    // real clip at upload time (see studio::plugins::UploadAnimationPlugin);
    // an out-of-sync cache after someone hand-edits the .anim file on disk
    // is a real, accepted limitation -- the source of truth remains the
    // file, this is a display convenience, not load-bearing anywhere.
    bool looping = true;
    float durationSeconds = 0.0f;

    // Real validation: non-empty id/name, and clipPath actually resolving
    // on disk -- the same "missing asset" check AvatarItem::validate()
    // already does for meshPath. Does NOT validate the clip's *content*
    // (joint targets, keyframe coverage) -- that needs a reference
    // Skeleton this struct doesn't have access to, see
    // core::validateAnimationClipAgainstSkeleton() in Animation.hpp for
    // that half.
    [[nodiscard]] bool validate(std::string& outError) const;
};

} // namespace engine::core
