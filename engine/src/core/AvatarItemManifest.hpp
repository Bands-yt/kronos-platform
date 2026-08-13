#pragma once

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

#include "core/AvatarItem.hpp"

namespace engine::core {

// The JSON-serializable package descriptor for exactly one AvatarItem --
// the same role core::PluginManifest plays for a Studio plugin, but real
// JSON instead of this codebase's usual hand-rolled "KEY value" text
// format (see cmake/Dependencies.cmake's nlohmann_json comment for why
// this is the deliberate exception). A manifest is what a creator
// authors/exports for one item (studio::UploadAvatarItemPlugin writes
// one); core::CatalogueDatabase persists many together as a JSON array
// of this same shape, reusing toJson()/fromJson() directly rather than
// round-tripping through temp files per entry.
struct AvatarItemManifest {
    AvatarItem item;
    std::string creatorId;
    int64_t uploadDateUnixSeconds = 0;
    // Stubbed currency unit, per this pass's explicit scope (task item 3:
    // "Add 'Purchase' button (stubbed for now)") -- no real economy
    // wiring here, same honesty level as marketplace::MarketplaceService's
    // own stated "routing layer, not a real payment flow" scope.
    int32_t price = 0;

    [[nodiscard]] bool saveToFile(const std::string& path) const;
    [[nodiscard]] bool loadFromFile(const std::string& path);

    [[nodiscard]] nlohmann::json toJson() const;
    // Populates `out` from `j`. Returns false (leaving `out` untouched)
    // if required fields are missing/malformed -- a corrupt catalogue
    // entry is skipped by CatalogueDatabase::loadFromFile(), not fatal to
    // loading the rest of the file, same fail-soft precedent as every
    // other loadFromFile() in this codebase.
    [[nodiscard]] static bool fromJson(const nlohmann::json& j, AvatarItemManifest& out);
};

} // namespace engine::core
