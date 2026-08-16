#include "core/AvatarItem.hpp"

#include <filesystem>

namespace engine::core {

const char* avatarItemCategoryName(AvatarItemCategory category) {
    switch (category) {
        case AvatarItemCategory::Head: return "Head";
        case AvatarItemCategory::Hair: return "Hair";
        case AvatarItemCategory::Face: return "Face";
        case AvatarItemCategory::Torso: return "Torso";
        case AvatarItemCategory::Legs: return "Legs";
        case AvatarItemCategory::Accessory: return "Accessory";
        case AvatarItemCategory::LayeredClothing: return "LayeredClothing";
        case AvatarItemCategory::Emote: return "Emote";
        case AvatarItemCategory::Shoes: return "Shoes";
        case AvatarItemCategory::Back: return "Back";
        case AvatarItemCategory::Bundle: return "Bundle";
    }
    return "Accessory";
}

bool avatarItemCategoryFromName(const std::string& name, AvatarItemCategory& out) {
    static constexpr AvatarItemCategory kAll[] = {
        AvatarItemCategory::Head,  AvatarItemCategory::Hair,  AvatarItemCategory::Face,  AvatarItemCategory::Torso,
        AvatarItemCategory::Legs,  AvatarItemCategory::Accessory, AvatarItemCategory::LayeredClothing,
        AvatarItemCategory::Emote, AvatarItemCategory::Shoes, AvatarItemCategory::Back, AvatarItemCategory::Bundle,
    };
    for (AvatarItemCategory candidate : kAll) {
        if (name == avatarItemCategoryName(candidate)) {
            out = candidate;
            return true;
        }
    }
    return false;
}

bool AvatarItem::validate(std::string& outError) const {
    if (id.empty()) {
        outError = "item id is empty";
        return false;
    }
    if (name.empty()) {
        outError = "item name is empty";
        return false;
    }
    if (meshPath.empty()) {
        outError = "no mesh assigned (meshPath is empty)";
        return false;
    }
    std::error_code ec;
    if (!std::filesystem::exists(meshPath, ec) || ec) {
        outError = "mesh file not found: " + meshPath;
        return false;
    }
    if (!texturePath.empty()) {
        if (!std::filesystem::exists(texturePath, ec) || ec) {
            outError = "texture file not found: " + texturePath;
            return false;
        }
    }
    if (metallic < 0.0f || metallic > 1.0f) {
        outError = "metallic must be in [0,1]";
        return false;
    }
    if (roughness < 0.0f || roughness > 1.0f) {
        outError = "roughness must be in [0,1]";
        return false;
    }
    return true;
}

} // namespace engine::core
