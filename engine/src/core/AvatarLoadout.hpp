#pragma once

#include <string>
#include <unordered_map>

#include "core/AvatarItem.hpp"

namespace engine::core {

class CatalogueIndex;

// Which catalogue item id is equipped in each category -- a map keyed by
// category, so "no duplicate categories" (a real, explicit requirement
// of this system) is structural, not a runtime check: a
// std::unordered_map physically cannot hold two values for the same key.
// LayeredClothing is the one category allowed to coexist *visually* with
// Torso/Legs (a jacket worn over a shirt) -- it's still just one more
// map entry like any other category, since this struct doesn't model
// "worn over" draw ordering itself; see AvatarLoadoutSync.hpp's
// applyLoadoutToAvatar() for how equipped items actually get spawned.
//
// Pure data + validation -- no Vulkan/ECS/mesh-loading here, same
// separation core::Prefab/core::SceneFile use: turning an equipped item
// id into a real, rendered ECS entity needs GPU handles this class
// deliberately doesn't own. See core/AvatarLoadoutSync.hpp for that half.
class AvatarLoadout {
public:
    // Equips `itemId` into whatever category it's filed under in
    // `index` (looked up there, not caller-supplied, so this loadout
    // never has to trust a mismatched category) -- replaces whatever was
    // already equipped in that category. Returns false (no change) if
    // `itemId` isn't found in `index`.
    [[nodiscard]] bool equip(const std::string& itemId, const CatalogueIndex& index);
    void unequip(AvatarItemCategory category);
    void clear() { equippedItems_.clear(); }

    [[nodiscard]] bool isEquipped(AvatarItemCategory category) const;
    [[nodiscard]] std::string equippedItemId(AvatarItemCategory category) const; // "" if nothing is equipped there

    [[nodiscard]] const std::unordered_map<AvatarItemCategory, std::string>& equippedItems() const {
        return equippedItems_;
    }

    // Real, structural validation beyond "no duplicate categories"
    // (already guaranteed by the map itself): confirms every equipped
    // item id still resolves in `index` (an item can be delisted from
    // the catalogue after being equipped) and that it's still filed
    // under the category it's equipped in (a catalogue item's own
    // category could in principle change between equip() and a later
    // validate() call). Returns false and fills `outError` with the
    // first problem found.
    [[nodiscard]] bool validate(const CatalogueIndex& index, std::string& outError) const;

    [[nodiscard]] bool saveToFile(const std::string& path) const;
    [[nodiscard]] bool loadFromFile(const std::string& path);

private:
    std::unordered_map<AvatarItemCategory, std::string> equippedItems_;
};

} // namespace engine::core
