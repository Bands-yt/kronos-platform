#include "core/AvatarItemManifest.hpp"

#include <fstream>
#include <sstream>

namespace engine::core {

namespace {

nlohmann::json vec4ToJson(const glm::vec4& v) { return nlohmann::json::array({v.x, v.y, v.z, v.w}); }

bool jsonToVec4(const nlohmann::json& j, glm::vec4& out) {
    if (!j.is_array() || j.size() != 4) return false;
    out = glm::vec4(j[0].get<float>(), j[1].get<float>(), j[2].get<float>(), j[3].get<float>());
    return true;
}

} // namespace

nlohmann::json AvatarItemManifest::toJson() const {
    nlohmann::json j;
    j["id"] = item.id;
    j["name"] = item.name;
    j["category"] = avatarItemCategoryName(item.category);
    j["tags"] = item.tags;
    j["meshPath"] = item.meshPath;
    j["texturePath"] = item.texturePath;
    j["baseColor"] = vec4ToJson(item.baseColor);
    j["metallic"] = item.metallic;
    j["roughness"] = item.roughness;
    j["creatorId"] = creatorId;
    j["uploadDateUnixSeconds"] = uploadDateUnixSeconds;
    j["price"] = price;
    return j;
}

bool AvatarItemManifest::fromJson(const nlohmann::json& j, AvatarItemManifest& out) {
    if (!j.is_object()) return false;
    // Every field access below is guarded by this one try/catch rather
    // than individual checks -- nlohmann's .get<T>() throws json::exception
    // on a type mismatch (a string field holding a number, etc.), and a
    // malformed catalogue entry should fail this one parse, not crash the
    // process -- same guarded-parsing discipline as core::ObjLoader.
    try {
        AvatarItemManifest parsed;
        if (!j.contains("id") || !j.contains("name")) return false;
        parsed.item.id = j.at("id").get<std::string>();
        parsed.item.name = j.at("name").get<std::string>();

        AvatarItemCategory category = AvatarItemCategory::Accessory;
        if (j.contains("category")) {
            // Unrecognized category name falls back to Accessory rather
            // than failing the whole load -- forward-compatible with a
            // future category this reader predates, same convention
            // every other loadFromFile() in this codebase uses for an
            // unrecognized enum index.
            (void)avatarItemCategoryFromName(j.at("category").get<std::string>(), category);
        }
        parsed.item.category = category;

        if (j.contains("tags")) parsed.item.tags = j.at("tags").get<std::vector<std::string>>();
        if (j.contains("meshPath")) parsed.item.meshPath = j.at("meshPath").get<std::string>();
        if (j.contains("texturePath")) parsed.item.texturePath = j.at("texturePath").get<std::string>();
        if (j.contains("baseColor")) jsonToVec4(j.at("baseColor"), parsed.item.baseColor);
        if (j.contains("metallic")) parsed.item.metallic = j.at("metallic").get<float>();
        if (j.contains("roughness")) parsed.item.roughness = j.at("roughness").get<float>();
        if (j.contains("creatorId")) parsed.creatorId = j.at("creatorId").get<std::string>();
        if (j.contains("uploadDateUnixSeconds")) {
            parsed.uploadDateUnixSeconds = j.at("uploadDateUnixSeconds").get<int64_t>();
        }
        if (j.contains("price")) parsed.price = j.at("price").get<int32_t>();

        out = std::move(parsed);
        return true;
    } catch (const nlohmann::json::exception&) {
        return false;
    }
}

bool AvatarItemManifest::saveToFile(const std::string& path) const {
    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) return false;
    out << toJson().dump(2);
    return out.good();
}

bool AvatarItemManifest::loadFromFile(const std::string& path) {
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
