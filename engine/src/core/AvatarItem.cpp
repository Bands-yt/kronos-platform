#include "core/AvatarItem.hpp"

#include <cctype>
#include <filesystem>

namespace engine::core {

namespace {
// Kronos ("Studio QoL Sprint" -- "catching absolute local path
// references... so all assets use relative package URIs"): real, pure,
// same logic as publishing::isAbsoluteAssetPath() (PublishValidation.cpp)
// -- duplicated here, not shared, since `core` (this file's own module)
// cannot depend on `publishing` (a higher-level module that already
// depends on `core`); the same small, local, per-file primitive
// duplication this codebase's own mesh-generation helpers already
// establish as the accepted convention for a genuinely tiny, stable
// check like this one.
bool isAbsoluteAssetPath(const std::string& path) {
    if (path.empty()) return false;
    if (path.front() == '/' || path.front() == '\\') return true;
    if (path.size() >= 3 && std::isalpha(static_cast<unsigned char>(path[0])) != 0 && path[1] == ':' &&
        (path[2] == '/' || path[2] == '\\')) {
        return true;
    }
    return false;
}
} // namespace

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
    // Kronos ("Studio QoL Sprint"): real, blocking -- an absolute path
    // here points at a location that only exists on the uploading
    // creator's own machine, so it silently breaks for anyone else once
    // this item is published (same reasoning
    // publishing::validateAssetPathsAreRelative() applies at the
    // world/scene level).
    if (isAbsoluteAssetPath(meshPath)) {
        outError = "mesh path must be a relative package path, not an absolute local path: " + meshPath;
        return false;
    }
    if (isAbsoluteAssetPath(texturePath)) {
        outError = "texture path must be a relative package path, not an absolute local path: " + texturePath;
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
