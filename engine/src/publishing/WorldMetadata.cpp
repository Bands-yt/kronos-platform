#include "publishing/WorldMetadata.hpp"

#include <fstream>
#include <sstream>

namespace engine::publishing {

const char* worldCategoryName(WorldCategory category) {
    switch (category) {
        case WorldCategory::Adventure: return "Adventure";
        case WorldCategory::Mining: return "Mining";
        case WorldCategory::Horror: return "Horror";
        case WorldCategory::Sandbox: return "Sandbox";
    }
    return "Sandbox";
}

bool worldCategoryFromName(const std::string& name, WorldCategory& out) {
    if (name == "Adventure") {
        out = WorldCategory::Adventure;
    } else if (name == "Mining") {
        out = WorldCategory::Mining;
    } else if (name == "Horror") {
        out = WorldCategory::Horror;
    } else if (name == "Sandbox") {
        out = WorldCategory::Sandbox;
    } else {
        return false;
    }
    return true;
}

nlohmann::json WorldMetadata::toJson() const {
    nlohmann::json j;
    j["title"] = title;
    j["description"] = description;
    j["tags"] = tags;
    j["creatorName"] = creatorName;
    j["recommendedPlayerCount"] = recommendedPlayerCount;
    j["category"] = worldCategoryName(category);
    j["thumbnailPath"] = thumbnailPath;
    return j;
}

bool WorldMetadata::fromJson(const nlohmann::json& j, WorldMetadata& out) {
    if (!j.is_object()) return false;
    try {
        WorldMetadata parsed;
        if (j.contains("title")) parsed.title = j.at("title").get<std::string>();
        if (j.contains("description")) parsed.description = j.at("description").get<std::string>();
        if (j.contains("tags")) parsed.tags = j.at("tags").get<std::vector<std::string>>();
        if (j.contains("creatorName")) parsed.creatorName = j.at("creatorName").get<std::string>();
        if (j.contains("recommendedPlayerCount")) parsed.recommendedPlayerCount = j.at("recommendedPlayerCount").get<int>();
        if (j.contains("category")) {
            // Unrecognized category name falls back to the real default
            // (Sandbox) rather than failing the whole load -- same
            // forward-compatible convention AvatarItemManifest::fromJson()
            // already established.
            (void)worldCategoryFromName(j.at("category").get<std::string>(), parsed.category);
        }
        if (j.contains("thumbnailPath")) parsed.thumbnailPath = j.at("thumbnailPath").get<std::string>();

        out = std::move(parsed);
        return true;
    } catch (const nlohmann::json::exception&) {
        return false;
    }
}

bool WorldMetadata::saveToFile(const std::string& path) const {
    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) return false;
    out << toJson().dump(2);
    return out.good();
}

bool WorldMetadata::loadFromFile(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) return false;
    std::stringstream buffer;
    buffer << in.rdbuf();

    nlohmann::json j;
    try {
        j = nlohmann::json::parse(buffer.str());
    } catch (const nlohmann::json::parse_error&) {
        return false;
    }
    return fromJson(j, *this);
}

} // namespace engine::publishing
