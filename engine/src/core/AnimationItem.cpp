#include "core/AnimationItem.hpp"

#include <filesystem>

namespace engine::core {

const char* animationCategoryName(AnimationCategory category) {
    switch (category) {
        case AnimationCategory::Emote: return "Emote";
        case AnimationCategory::Locomotion: return "Locomotion";
        case AnimationCategory::Misc: return "Misc";
    }
    return "Misc";
}

bool animationCategoryFromName(const std::string& name, AnimationCategory& out) {
    static constexpr AnimationCategory kAll[] = {AnimationCategory::Emote, AnimationCategory::Locomotion,
                                                  AnimationCategory::Misc};
    for (AnimationCategory candidate : kAll) {
        if (name == animationCategoryName(candidate)) {
            out = candidate;
            return true;
        }
    }
    return false;
}

bool AnimationItem::validate(std::string& outError) const {
    if (id.empty()) {
        outError = "animation id is empty";
        return false;
    }
    if (name.empty()) {
        outError = "animation name is empty";
        return false;
    }
    if (clipPath.empty()) {
        outError = "no clip assigned (clipPath is empty)";
        return false;
    }
    std::error_code ec;
    if (!std::filesystem::exists(clipPath, ec) || ec) {
        outError = "clip file not found: " + clipPath;
        return false;
    }
    if (durationSeconds < 0.0f) {
        outError = "durationSeconds must be non-negative";
        return false;
    }
    return true;
}

} // namespace engine::core
