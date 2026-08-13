#include "core/CatalogueDatabase.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace engine::core {

void CatalogueDatabase::upsert(AvatarItemManifest entry) {
    auto it = std::find_if(entries_.begin(), entries_.end(),
                            [&](const AvatarItemManifest& e) { return e.item.id == entry.item.id; });
    if (it != entries_.end()) {
        *it = std::move(entry);
    } else {
        entries_.push_back(std::move(entry));
    }
}

bool CatalogueDatabase::remove(const std::string& itemId) {
    auto it = std::find_if(entries_.begin(), entries_.end(),
                            [&](const AvatarItemManifest& e) { return e.item.id == itemId; });
    if (it == entries_.end()) return false;
    entries_.erase(it);
    return true;
}

bool CatalogueDatabase::saveToFile(const std::string& path) const {
    nlohmann::json array = nlohmann::json::array();
    for (const auto& entry : entries_) array.push_back(entry.toJson());

    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) return false;
    out << array.dump(2);
    return out.good();
}

bool CatalogueDatabase::loadFromFile(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) return false;
    std::stringstream buffer;
    buffer << in.rdbuf();

    nlohmann::json array;
    try {
        array = nlohmann::json::parse(buffer.str());
    } catch (const nlohmann::json::parse_error&) {
        return false;
    }
    if (!array.is_array()) return false;

    std::vector<AvatarItemManifest> loaded;
    loaded.reserve(array.size());
    for (const auto& element : array) {
        AvatarItemManifest entry;
        if (AvatarItemManifest::fromJson(element, entry)) {
            loaded.push_back(std::move(entry));
        }
        // A malformed individual entry is skipped, see header comment.
    }
    entries_ = std::move(loaded);
    return true;
}

} // namespace engine::core
