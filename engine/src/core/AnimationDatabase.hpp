#pragma once

#include <string>
#include <vector>

#include "core/AnimationManifest.hpp"

namespace engine::core {

// Persistent storage for every uploaded animation clip's
// AnimationManifest, together, in one JSON file -- the animation-clip
// counterpart to core::CatalogueDatabase (same "one file, whole database"
// shape, same reasoning -- see its header comment).
// studio::plugins::UploadAnimationPlugin upserts into this on a
// successful upload. No separate AnimationIndex/search structure --
// unlike the avatar catalogue (browsed by a real search UI over
// thousands of hypothetical items), this pass's real consumers (the
// Emote System's catalogue integration, see EmoteSystem.hpp) only ever
// need "every entry" or "every entry in one category," both a direct
// linear scan over entries() handles fine at this scale; a dedicated
// index would be speculative machinery ahead of an actual need.
class AnimationDatabase {
public:
    [[nodiscard]] bool saveToFile(const std::string& path) const;
    // A malformed individual entry inside an otherwise-valid JSON array is
    // skipped, not fatal to the rest of the load -- same fail-soft
    // precedent as CatalogueDatabase::loadFromFile(). Returns false only
    // if the file can't be opened or isn't a JSON array at all.
    [[nodiscard]] bool loadFromFile(const std::string& path);

    // Appends a new entry, or replaces it in place if an entry with the
    // same item.id already exists -- same upsert semantics as
    // CatalogueDatabase::upsert().
    void upsert(AnimationManifest entry);
    [[nodiscard]] bool remove(const std::string& id);

    [[nodiscard]] const AnimationManifest* findById(const std::string& id) const;

    [[nodiscard]] const std::vector<AnimationManifest>& entries() const { return entries_; }
    [[nodiscard]] size_t size() const { return entries_.size(); }

private:
    std::vector<AnimationManifest> entries_;
};

} // namespace engine::core
