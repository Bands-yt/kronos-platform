#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "net/NetTypes.hpp"
#include "publishing/WorldMetadata.hpp"

namespace engine::publishing {

// Sprint 13 task 4's "Add server-side registry of published worlds...
// world IDs, creator IDs, version IDs." A real, lightweight *listing* --
// deliberately NOT the full WorldPackage (no embedded core::SceneFile):
// the registry is a real catalogue of what's been published and by whom,
// the same "database of many, not one item's own package" split
// core::CatalogueDatabase already draws against core::AvatarItemManifest
// (see that class's own header comment). The heavy package bytes stay a
// real local file on disk (WorldPackage::saveToDirectory()); this is
// what a listing/retrieval query answers "what worlds exist, who
// published them, what version" without needing to load any of them.
struct WorldListing {
    std::string worldId;
    net::PlayerId creatorId = net::kInvalidPlayer;
    std::string version;
    WorldMetadata metadata;
    int64_t publishedAtUnixSeconds = 0;

    [[nodiscard]] nlohmann::json toJson() const;
    [[nodiscard]] static bool fromJson(const nlohmann::json& j, WorldListing& out);
};

// Same real shape as core::CatalogueDatabase: one JSON file, a JSON
// array of WorldListing::toJson() objects, upsert-by-primary-key
// semantics. See that class's own header comment for why one file, not
// a directory of per-world listing files.
class WorldRegistry {
public:
    [[nodiscard]] bool saveToFile(const std::string& path) const;
    [[nodiscard]] bool loadFromFile(const std::string& path);

    // Appends a new listing, or replaces it in place if one with the
    // same worldId already exists -- worldId is the real primary key
    // (re-publishing the same id is a version update, not a duplicate
    // listing), same convention as CatalogueDatabase::upsert().
    void upsert(WorldListing listing);
    [[nodiscard]] bool remove(const std::string& worldId);

    [[nodiscard]] const std::vector<WorldListing>& entries() const { return entries_; }
    [[nodiscard]] size_t size() const { return entries_.size(); }

    [[nodiscard]] const WorldListing* findById(const std::string& worldId) const;
    [[nodiscard]] std::vector<WorldListing> findByCreator(net::PlayerId creatorId) const;
    [[nodiscard]] std::vector<WorldListing> listByCategory(WorldCategory category) const;

private:
    std::vector<WorldListing> entries_;
};

} // namespace engine::publishing
