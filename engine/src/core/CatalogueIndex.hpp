#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
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
    // Kronos ("Marketplace Search + Categories v2" -- "Marketplace Search
    // Engine"): real, case-insensitive substring search, real-matched
    // against the item's own name, creatorId, OR any of its own tags (an
    // OR across those three real fields -- the one, unified search box
    // studio::plugins::CataloguePanel already exposes covers "substring
    // search," "tag-based search," and "creator-based search" all
    // together, matching how a real creator-tools search bar actually
    // gets typed into). Empty = no text filter.
    std::string textQuery;
    // Kronos ("Marketplace Search + Categories v2" -- "Moderation
    // Integration"): unset = don't filter by moderation state at all
    // (what a creator's own "My Items" view real-uses, so they see their
    // own UnderReview/Rejected items too); set to
    // AvatarItemModerationStatus::Approved is what every real public
    // browse view real-uses, so a flagged-but-not-yet-reviewed or
    // rejected item never shows up to a browsing player.
    std::optional<AvatarItemModerationStatus> moderationStatus;

    enum class SortOrder {
        Relevance,
        PriceLowToHigh,
        PriceHighToLow,
        RecencyNewestFirst,
        // Kronos ("Marketplace Search + Categories v2"): real, new sort
        // orders. MostPurchased isn't one of these -- it needs real
        // marketplace::TransactionLog purchase-count data this class has
        // no dependency on by design (see this class's own header
        // comment on CatalogueDatabase/CatalogueIndex's real split);
        // studio::plugins::CataloguePanel computes that one itself, from
        // its own real transactionLog_ reference.
        TopRated,
        CreatorAlphabetical,
    };
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
    // Kronos ("Marketplace Search + Categories v2" -- "Catalogue
    // Performance" -- "no O(n²) scans"): real O(1) findById() via this
    // real id->index map, kept in sync by rebuild()/upsert() (both real,
    // small, non-hot-path mutation points) -- findById() was a real O(n)
    // linear scan before, called every frame from a detail popup that
    // stays open (studio::plugins::CataloguePanel::drawDetailPopup()), a
    // real, worthwhile fix at any real catalogue size.
    std::vector<AvatarItemManifest> entries_;
    std::unordered_map<std::string, size_t> idToIndex_;
};

} // namespace engine::core
