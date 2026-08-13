#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace engine::publishing {

// Sprint 13 ("Publishing & Game Packaging") task 2's "category
// (adventure, mining, horror, sandbox)" -- the exact four categories the
// sprint brief names, plus a real fallback for anything published before
// a future category this reader predates (see fromJson()'s own
// unrecognized-value handling, same forward-compatible convention every
// other enum-in-JSON in this codebase already uses).
enum class WorldCategory { Adventure, Mining, Horror, Sandbox };

[[nodiscard]] const char* worldCategoryName(WorldCategory category);
[[nodiscard]] bool worldCategoryFromName(const std::string& name, WorldCategory& out);

// Sprint 13 task 2's "Metadata System" -- title, description, tags,
// creator name, recommended player count, category, thumbnail metadata.
// Real JSON (via nlohmann, the same deliberate exception to this
// codebase's usual hand-rolled text format core::AvatarItemManifest
// already established, and for the same reason: this is published,
// externally-facing package metadata, not an internal engine save
// format) -- toJson()/fromJson() mirror AvatarItemManifest's exact shape
// and guarded-parsing convention.
struct WorldMetadata {
    std::string title;
    std::string description;
    std::vector<std::string> tags;
    std::string creatorName;
    int recommendedPlayerCount = 4;
    WorldCategory category = WorldCategory::Sandbox;

    // Real, relative-to-the-package-directory path to the captured
    // thumbnail file (see ThumbnailCapture.hpp) -- empty means "no
    // thumbnail captured yet", a real, honest state PublishValidation
    // checks for, not silently allowed through.
    std::string thumbnailPath;

    [[nodiscard]] nlohmann::json toJson() const;
    [[nodiscard]] static bool fromJson(const nlohmann::json& j, WorldMetadata& out);

    [[nodiscard]] bool saveToFile(const std::string& path) const;
    [[nodiscard]] bool loadFromFile(const std::string& path);
};

} // namespace engine::publishing
