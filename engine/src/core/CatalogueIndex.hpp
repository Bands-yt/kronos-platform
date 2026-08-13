#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/AvatarItem.hpp"
#include "core/AvatarItemManifest.hpp"

namespace engine::core {

class CatalogueDatabase;

// What to filter/sort a catalogue search by -- every field is optional
// (unset = don't filter on this). Multiple tags are AND-matched (an item
// must have all of them, not any), matching how a creator-tools search
// bar typically *narrows* results rather than broadens them.
struct CatalogueSearchFilter {
    std::optional<AvatarItemCategory> category;
    std::vector<std::string> tags;
    std::string creatorId; // empty = any creator
    std::optional<int32_t> minPrice;
    std::optional<int32_t> maxPrice;

    enum class SortOrder { Relevance, PriceLowToHigh, PriceHighToLow, RecencyNewestFirst };
    SortOrder sortOrder = SortOrder::Relevance;
};

// A queryable, in-memory view over a CatalogueDatabase's entries --
// separate from the database itself the same way core::AssetCache is
// separate from the files it caches: CatalogueDatabase owns persistence
// (load/save/upsert/remove to/from disk), CatalogueIndex owns
// *searching*. rebuild() copies the database's current entries in;
// nothing here writes back to the database or touches disk.
class CatalogueIndex {
public:
    void rebuild(const CatalogueDatabase& database);

    // Add/update a single entry without a full rebuild() -- what
    // studio::UploadAvatarItemPlugin calls right after
    // CatalogueDatabase::upsert() so a freshly-uploaded item shows up in
    // the Catalogue Viewer immediately, not only after the next full
    // rebuild.
    void upsert(AvatarItemManifest entry);

    // Returned pointers alias this index's own internal storage and are
    // only valid until the next rebuild()/upsert() call (either can
    // reallocate) -- callers that need a result to outlive that (e.g. a
    // "Try On" button forwarding an item to the Avatar Previewer) should
    // copy the pointed-to AvatarItemManifest, not hold the pointer.
    [[nodiscard]] std::vector<const AvatarItemManifest*> search(const CatalogueSearchFilter& filter) const;
    [[nodiscard]] const AvatarItemManifest* findById(const std::string& itemId) const;

    [[nodiscard]] size_t size() const { return entries_.size(); }

private:
    std::vector<AvatarItemManifest> entries_;
};

} // namespace engine::core
