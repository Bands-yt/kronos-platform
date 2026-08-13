#include "core/EmoteSystem.hpp"

namespace engine::core {

bool resolveEmoteClip(const std::string& itemId, const AnimationDatabase& animationDatabase, AnimationClip& outClip,
                       std::string& outError) {
    const AnimationManifest* manifest = animationDatabase.findById(itemId);
    if (manifest == nullptr) {
        outError = "no animation database entry for id \"" + itemId + "\" -- the avatar catalogue and animation "
                    "database entries for an emote must share the same id, see EmoteSystem.hpp";
        return false;
    }
    if (manifest->item.category != AnimationCategory::Emote) {
        outError = "animation \"" + itemId + "\" exists but is not categorized as Emote";
        return false;
    }

    AnimationClip loaded;
    if (!loaded.loadFromFile(manifest->item.clipPath)) {
        outError = "emote \"" + itemId + "\" is listed but its clip file failed to load: " + manifest->item.clipPath;
        return false;
    }

    outClip = std::move(loaded);
    return true;
}

bool playEquippedEmote(const AvatarLoadout& loadout, const AnimationDatabase& animationDatabase,
                        AvatarController& controller, std::string& outError) {
    outError.clear();
    std::string emoteId = loadout.equippedItemId(AvatarItemCategory::Emote);
    if (emoteId.empty()) {
        controller.stopEmote();
        return false;
    }

    AnimationClip clip;
    if (!resolveEmoteClip(emoteId, animationDatabase, clip, outError)) {
        controller.stopEmote();
        return false;
    }

    bool looping = clip.looping; // honor the clip's own authored looping flag, not a hardcoded assumption
    controller.playEmote(std::move(clip), looping, /*fullBody=*/true);
    return true;
}

} // namespace engine::core
