#include "core/AnimationDatabase.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace engine::core {

void AnimationDatabase::upsert(AnimationManifest entry) {
    auto it = std::find_if(entries_.begin(), entries_.end(),
                            [&](const AnimationManifest& e) { return e.item.id == entry.item.id; });
    if (it != entries_.end()) {
        *it = std::move(entry);
    } else {
        entries_.push_back(std::move(entry));
    }
}

bool AnimationDatabase::remove(const std::string& id) {
    auto it = std::find_if(entries_.begin(), entries_.end(), [&](const AnimationManifest& e) { return e.item.id == id; });
    if (it == entries_.end()) return false;
    entries_.erase(it);
    return true;
}

const AnimationManifest* AnimationDatabase::findById(const std::string& id) const {
    auto it = std::find_if(entries_.begin(), entries_.end(), [&](const AnimationManifest& e) { return e.item.id == id; });
    return it != entries_.end() ? &(*it) : nullptr;
}

bool AnimationDatabase::saveToFile(const std::string& path) const {
    nlohmann::json array = nlohmann::json::array();
    for (const auto& entry : entries_) array.push_back(entry.toJson());

    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) return false;
    out << array.dump(2);
    return out.good();
}

bool AnimationDatabase::loadFromFile(const std::string& path) {
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

    std::vector<AnimationManifest> loaded;
    loaded.reserve(array.size());
    for (const auto& element : array) {
        AnimationManifest entry;
        if (AnimationManifest::fromJson(element, entry)) {
            loaded.push_back(std::move(entry));
        }
    }
    entries_ = std::move(loaded);
    return true;
}

} // namespace engine::core
