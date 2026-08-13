#include "core/AvatarLoadout.hpp"

#include <fstream>
#include <sstream>

#include "core/CatalogueIndex.hpp"

namespace engine::core {

bool AvatarLoadout::equip(const std::string& itemId, const CatalogueIndex& index) {
    const AvatarItemManifest* entry = index.findById(itemId);
    if (entry == nullptr) return false;
    equippedItems_[entry->item.category] = itemId;
    return true;
}

void AvatarLoadout::unequip(AvatarItemCategory category) { equippedItems_.erase(category); }

bool AvatarLoadout::isEquipped(AvatarItemCategory category) const { return equippedItems_.count(category) != 0; }

std::string AvatarLoadout::equippedItemId(AvatarItemCategory category) const {
    auto it = equippedItems_.find(category);
    return it != equippedItems_.end() ? it->second : std::string();
}

bool AvatarLoadout::validate(const CatalogueIndex& index, std::string& outError) const {
    for (const auto& [category, itemId] : equippedItems_) {
        const AvatarItemManifest* entry = index.findById(itemId);
        if (entry == nullptr) {
            outError =
                "equipped item \"" + itemId + "\" (" + avatarItemCategoryName(category) + ") is not in the catalogue";
            return false;
        }
        if (entry->item.category != category) {
            outError = "equipped item \"" + itemId + "\" is filed under " + avatarItemCategoryName(entry->item.category) +
                        " but is equipped as " + avatarItemCategoryName(category);
            return false;
        }
    }
    return true;
}

bool AvatarLoadout::saveToFile(const std::string& path) const {
    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) return false;

    out << "LOADOUT 1\n";
    for (const auto& [category, itemId] : equippedItems_) {
        out << "EQUIP " << avatarItemCategoryName(category) << ' ' << itemId << "\n";
    }
    out << "END\n";
    return out.good();
}

bool AvatarLoadout::loadFromFile(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) return false;

    std::string header;
    if (!std::getline(in, header) || header.rfind("LOADOUT", 0) != 0) return false;

    std::unordered_map<AvatarItemCategory, std::string> loaded;
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("EQUIP ", 0) == 0) {
            std::istringstream iss(line.substr(6));
            std::string categoryName;
            iss >> categoryName;
            std::string itemId;
            std::getline(iss, itemId);
            if (!itemId.empty() && itemId.front() == ' ') itemId.erase(itemId.begin());

            AvatarItemCategory category;
            if (!itemId.empty() && avatarItemCategoryFromName(categoryName, category)) {
                loaded[category] = itemId;
            }
            // An unrecognized category name or empty item id silently
            // skips that one EQUIP line -- forward-compatible with a
            // future category this reader predates, same convention as
            // every other loadFromFile() in this codebase.
        } else if (line == "END") {
            break;
        }
    }

    equippedItems_ = std::move(loaded);
    return true;
}

} // namespace engine::core
