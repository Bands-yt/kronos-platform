#include "publishing/WorldRegistry.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace engine::publishing {

nlohmann::json WorldListing::toJson() const {
    nlohmann::json j;
    j["worldId"] = worldId;
    j["creatorId"] = creatorId;
    j["version"] = version;
    j["metadata"] = metadata.toJson();
    j["publishedAtUnixSeconds"] = publishedAtUnixSeconds;
    return j;
}

bool WorldListing::fromJson(const nlohmann::json& j, WorldListing& out) {
    if (!j.is_object()) return false;
    try {
        WorldListing parsed;
        if (!j.contains("worldId")) return false;
        parsed.worldId = j.at("worldId").get<std::string>();
        if (j.contains("creatorId")) parsed.creatorId = j.at("creatorId").get<net::PlayerId>();
        if (j.contains("version")) parsed.version = j.at("version").get<std::string>();
        if (j.contains("metadata")) {
            if (!WorldMetadata::fromJson(j.at("metadata"), parsed.metadata)) return false;
        }
        if (j.contains("publishedAtUnixSeconds")) parsed.publishedAtUnixSeconds = j.at("publishedAtUnixSeconds").get<int64_t>();

        out = std::move(parsed);
        return true;
    } catch (const nlohmann::json::exception&) {
        return false;
    }
}

bool WorldRegistry::saveToFile(const std::string& path) const {
    nlohmann::json array = nlohmann::json::array();
    for (const auto& entry : entries_) array.push_back(entry.toJson());

    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) return false;
    out << array.dump(2);
    return out.good();
}

bool WorldRegistry::loadFromFile(const std::string& path) {
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

    std::vector<WorldListing> loaded;
    for (const auto& element : array) {
        // A malformed individual entry is skipped, not fatal to the rest
        // of the load -- same fail-soft precedent
        // CatalogueDatabase::loadFromFile() already established.
        WorldListing listing;
        if (WorldListing::fromJson(element, listing)) loaded.push_back(std::move(listing));
    }
    entries_ = std::move(loaded);
    return true;
}

void WorldRegistry::upsert(WorldListing listing) {
    for (auto& entry : entries_) {
        if (entry.worldId == listing.worldId) {
            entry = std::move(listing);
            return;
        }
    }
    entries_.push_back(std::move(listing));
}

bool WorldRegistry::remove(const std::string& worldId) {
    auto it = std::find_if(entries_.begin(), entries_.end(), [&](const WorldListing& e) { return e.worldId == worldId; });
    if (it == entries_.end()) return false;
    entries_.erase(it);
    return true;
}

const WorldListing* WorldRegistry::findById(const std::string& worldId) const {
    for (const auto& entry : entries_) {
        if (entry.worldId == worldId) return &entry;
    }
    return nullptr;
}

std::vector<WorldListing> WorldRegistry::findByCreator(net::PlayerId creatorId) const {
    std::vector<WorldListing> result;
    for (const auto& entry : entries_) {
        if (entry.creatorId == creatorId) result.push_back(entry);
    }
    return result;
}

std::vector<WorldListing> WorldRegistry::listByCategory(WorldCategory category) const {
    std::vector<WorldListing> result;
    for (const auto& entry : entries_) {
        if (entry.metadata.category == category) result.push_back(entry);
    }
    return result;
}

} // namespace engine::publishing
