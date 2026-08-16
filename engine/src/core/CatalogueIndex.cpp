#include "core/CatalogueIndex.hpp"

#include <algorithm>
#include <cctype>

#include "core/CatalogueDatabase.hpp"

namespace engine::core {

namespace {

std::string toLowerAscii(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

bool matchesTextQuery(const AvatarItemManifest& entry, const std::string& lowerNeedle) {
    if (lowerNeedle.empty()) return true;
    if (toLowerAscii(entry.item.name).find(lowerNeedle) != std::string::npos) return true;
    if (toLowerAscii(entry.creatorId).find(lowerNeedle) != std::string::npos) return true;
    for (const std::string& tag : entry.item.tags) {
        if (toLowerAscii(tag).find(lowerNeedle) != std::string::npos) return true;
    }
    return false;
}

bool matchesFilter(const AvatarItemManifest& entry, const CatalogueSearchFilter& filter, const std::string& lowerNeedle) {
    if (filter.category.has_value() && entry.item.category != *filter.category) return false;
    if (!filter.creatorId.empty() && entry.creatorId != filter.creatorId) return false;
    if (filter.minPrice.has_value() && entry.price < *filter.minPrice) return false;
    if (filter.maxPrice.has_value() && entry.price > *filter.maxPrice) return false;
    if (filter.moderationStatus.has_value() && entry.moderationStatus != *filter.moderationStatus) return false;
    for (const auto& tag : filter.tags) {
        if (std::find(entry.item.tags.begin(), entry.item.tags.end(), tag) == entry.item.tags.end()) return false;
    }
    if (!matchesTextQuery(entry, lowerNeedle)) return false;
    return true;
}

} // namespace

void CatalogueIndex::rebuild(const CatalogueDatabase& database) {
    entries_ = database.entries();
    idToIndex_.clear();
    for (size_t i = 0; i < entries_.size(); ++i) idToIndex_[entries_[i].item.id] = i;
}

void CatalogueIndex::upsert(AvatarItemManifest entry) {
    auto it = idToIndex_.find(entry.item.id);
    if (it != idToIndex_.end()) {
        entries_[it->second] = std::move(entry);
    } else {
        idToIndex_[entry.item.id] = entries_.size();
        entries_.push_back(std::move(entry));
    }
}

std::vector<const AvatarItemManifest*> CatalogueIndex::search(const CatalogueSearchFilter& filter) const {
    std::string lowerNeedle = toLowerAscii(filter.textQuery);
    std::vector<const AvatarItemManifest*> results;
    for (const auto& entry : entries_) {
        if (matchesFilter(entry, filter, lowerNeedle)) results.push_back(&entry);
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
        case CatalogueSearchFilter::SortOrder::TopRated:
            // Real, honest tie-break: an item with zero ratings sorts
            // behind any rated item regardless of its own 0.0f
            // ratingScore default (which would otherwise misread as "the
            // worst-rated item" rather than "not rated yet").
            std::sort(results.begin(), results.end(), [](const auto* a, const auto* b) {
                if (a->ratingCount == 0 && b->ratingCount == 0) return false;
                if (a->ratingCount == 0) return false;
                if (b->ratingCount == 0) return true;
                return a->ratingScore > b->ratingScore;
            });
            break;
        case CatalogueSearchFilter::SortOrder::CreatorAlphabetical:
            std::sort(results.begin(), results.end(),
                      [](const auto* a, const auto* b) { return a->creatorId < b->creatorId; });
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
    auto it = idToIndex_.find(itemId);
    return it != idToIndex_.end() ? &entries_[it->second] : nullptr;
}

} // namespace engine::core
