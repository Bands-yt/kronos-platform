#pragma once

#include <string>
#include <vector>

#include "core/AvatarItemManifest.hpp"

namespace engine::core {

// Persistent storage for the whole catalogue -- every uploaded item's
// AvatarItemManifest, together, in one JSON file (a JSON array of
// AvatarItemManifest::toJson() objects). Deliberately one file, not a
// directory of per-item manifests: this is a *database*, meant to be
// loaded/saved and queried as a whole (via core::CatalogueIndex, see its
// header), whereas a single AvatarItemManifest file is what a creator
// authors/exports for one item in isolation (see its own header
// comment). studio::UploadAvatarItemPlugin upserts into this on a
// successful upload; the two share their JSON shape (toJson/fromJson)
// but serve different purposes -- one item's package vs. the whole
// catalogue's record.
class CatalogueDatabase {
public:
    [[nodiscard]] bool saveToFile(const std::string& path) const;
    // A malformed individual entry inside an otherwise-valid JSON array
    // is skipped, not fatal to the rest of the load -- same fail-soft
    // precedent as every other loadFromFile() in this codebase. Returns
    // false only if the file can't be opened or isn't a JSON array at
    // all.
    [[nodiscard]] bool loadFromFile(const std::string& path);

    // Appends a new entry, or replaces it in place if an entry with the
    // same item.id already exists -- ids are the catalogue's real
    // primary key (see AvatarItem::id's comment), so re-uploading the
    // same id is an update, not a duplicate listing.
    void upsert(AvatarItemManifest entry);
    [[nodiscard]] bool remove(const std::string& itemId);

    [[nodiscard]] const std::vector<AvatarItemManifest>& entries() const { return entries_; }
    [[nodiscard]] size_t size() const { return entries_.size(); }

private:
    std::vector<AvatarItemManifest> entries_;
};

} // namespace engine::core
