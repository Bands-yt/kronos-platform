#include "core/CatalogueIndex.hpp"

#include <algorithm>

#include "core/CatalogueDatabase.hpp"

namespace engine::core {

namespace {

bool matchesFilter(const AvatarItemManifest& entry, const CatalogueSearchFilter& filter) {
    if (filter.category.has_value() && entry.item.category != *filter.category) return false;
    if (!filter.creatorId.empty() && entry.creatorId != filter.creatorId) return false;
    if (filter.minPrice.has_value() && entry.price < *filter.minPrice) return false;
    if (filter.maxPrice.has_value() && entry.price > *filter.maxPrice) return false;
    for (const auto& tag : filter.tags) {
        if (std::find(entry.item.tags.begin(), entry.item.tags.end(), tag) == entry.item.tags.end()) return false;
    }
    return true;
}

} // namespace

void CatalogueIndex::rebuild(const CatalogueDatabase& database) { entries_ = database.entries(); }

void CatalogueIndex::upsert(AvatarItemManifest entry) {
    auto it = std::find_if(entries_.begin(), entries_.end(),
                            [&](const AvatarItemManifest& e) { return e.item.id == entry.item.id; });
    if (it != entries_.end()) {
        *it = std::move(entry);
    } else {
        entries_.push_back(std::move(entry));
    }
}

std::vector<const AvatarItemManifest*> CatalogueIndex::search(const CatalogueSearchFilter& filter) const {
    std::vector<const AvatarItemManifest*> results;
    for (const auto& entry : entries_) {
        if (matchesFilter(entry, filter)) results.push_back(&entry);
    }

    switch (filter.sortOrder) {
        case CatalogueSearchFilter::SortOrder::PriceLowToHigh:
            std::sort(results.begin(), results.end(), [](const auto* a, const auto* b) { return a->price < b->price; });
            break;
        case CatalogueSearchFilter::SortOrder::PriceHighToLow:
            std::sort(results.begin(), results.end(), [](const auto* a, const auto* b) { return a->price > b->price; });
            break;
        case CatalogueSearchFilter::SortOrder::RecencyNewestFirst:
            std::sort(results.begin(), results.end(),
                      [](const auto* a, const auto* b) { return a->uploadDateUnixSeconds > b->uploadDateUnixSeconds; });
            break;
        case CatalogueSearchFilter::SortOrder::Relevance:
            // No relevance-scoring model exists yet (would need a real
            // text-search ranking, e.g. TF-IDF over name/tags) -- stated
            // plainly rather than faked; results stay in the index's
            // insertion order for this case.
            break;
    }
    return results;
}

const AvatarItemManifest* CatalogueIndex::findById(const std::string& itemId) const {
    for (const auto& entry : entries_) {
        if (entry.item.id == itemId) return &entry;
    }
    return nullptr;
}

} // namespace engine::core
