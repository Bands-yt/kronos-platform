#pragma once

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

#include "core/AnimationItem.hpp"

namespace engine::core {

// The JSON-serializable package descriptor for exactly one AnimationItem
// -- the animation-clip counterpart to core::AvatarItemManifest, same
// shape and same reasoning (see that header's comment for why real JSON,
// not this codebase's usual hand-rolled text format, is the deliberate
// choice here). studio::plugins::UploadAnimationPlugin writes one per
// upload; core::AnimationDatabase persists many together as a JSON array
// of this same shape.
struct AnimationManifest {
    AnimationItem item;
    std::string creatorId;
    int64_t uploadDateUnixSeconds = 0;

    [[nodiscard]] bool saveToFile(const std::string& path) const;
    [[nodiscard]] bool loadFromFile(const std::string& path);

    [[nodiscard]] nlohmann::json toJson() const;
    // Populates `out` from `j`. Returns false (leaving `out` untouched) if
    // required fields are missing/malformed -- a corrupt database entry is
    // skipped by AnimationDatabase::loadFromFile(), not fatal to loading
    // the rest of the file, same fail-soft precedent as
    // AvatarItemManifest::fromJson().
    [[nodiscard]] static bool fromJson(const nlohmann::json& j, AnimationManifest& out);
};

} // namespace engine::core
