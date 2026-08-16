#include "core/AvatarIdleClipResolution.hpp"

namespace engine::core {

std::string resolveAvatarIdleClipPath(const LocalProfile& localProfile, const AnimationDatabase& animationDatabase,
                                       const std::string& shippedIdleClipPath) {
    if (!localProfile.animOverrideIdleId.empty()) {
        const AnimationManifest* manifest = animationDatabase.findById(localProfile.animOverrideIdleId);
        if (manifest != nullptr) return manifest->item.clipPath;
    }
    return shippedIdleClipPath;
}

} // namespace engine::core
