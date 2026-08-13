#include "core/AnimationManifest.hpp"

#include <fstream>
#include <sstream>

namespace engine::core {

nlohmann::json AnimationManifest::toJson() const {
    nlohmann::json j;
    j["id"] = item.id;
    j["name"] = item.name;
    j["category"] = animationCategoryName(item.category);
    j["tags"] = item.tags;
    j["clipPath"] = item.clipPath;
    j["looping"] = item.looping;
    j["durationSeconds"] = item.durationSeconds;
    j["creatorId"] = creatorId;
    j["uploadDateUnixSeconds"] = uploadDateUnixSeconds;
    return j;
}

bool AnimationManifest::fromJson(const nlohmann::json& j, AnimationManifest& out) {
    if (!j.is_object()) return false;
    // Same one-try/catch-guards-every-field discipline as
    // AvatarItemManifest::fromJson() -- see its comment.
    try {
        AnimationManifest parsed;
        if (!j.contains("id") || !j.contains("name")) return false;
        parsed.item.id = j.at("id").get<std::string>();
        parsed.item.name = j.at("name").get<std::string>();

        AnimationCategory category = AnimationCategory::Misc;
        if (j.contains("category")) {
            (void)animationCategoryFromName(j.at("category").get<std::string>(), category);
        }
        parsed.item.category = category;

        if (j.contains("tags")) parsed.item.tags = j.at("tags").get<std::vector<std::string>>();
        if (j.contains("clipPath")) parsed.item.clipPath = j.at("clipPath").get<std::string>();
        if (j.contains("looping")) parsed.item.looping = j.at("looping").get<bool>();
        if (j.contains("durationSeconds")) parsed.item.durationSeconds = j.at("durationSeconds").get<float>();
        if (j.contains("creatorId")) parsed.creatorId = j.at("creatorId").get<std::string>();
        if (j.contains("uploadDateUnixSeconds")) {
            parsed.uploadDateUnixSeconds = j.at("uploadDateUnixSeconds").get<int64_t>();
        }

        out = std::move(parsed);
        return true;
    } catch (const nlohmann::json::exception&) {
        return false;
    }
}

bool AnimationManifest::saveToFile(const std::string& path) const {
    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) return false;
    out << toJson().dump(2);
    return out.good();
}

bool AnimationManifest::loadFromFile(const std::string& path) {
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

} // namespace engine::core
