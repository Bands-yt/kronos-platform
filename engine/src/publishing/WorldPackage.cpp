#include "publishing/WorldPackage.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

namespace engine::publishing {

std::string WorldPackage::scenePath(const std::string& directory) { return directory + "/scene.txt"; }
std::string WorldPackage::metadataPath(const std::string& directory) { return directory + "/metadata.json"; }
std::string WorldPackage::packageInfoPath(const std::string& directory) { return directory + "/package.json"; }

bool WorldPackage::saveToDirectory(const std::string& directory) const {
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (ec) return false;

    if (!scene.saveToFile(scenePath(directory))) return false;
    if (!metadata.saveToFile(metadataPath(directory))) return false;

    nlohmann::json packageInfo;
    packageInfo["worldId"] = worldId;
    packageInfo["version"] = version;
    std::ofstream out(packageInfoPath(directory), std::ios::trunc);
    if (!out.is_open()) return false;
    out << packageInfo.dump(2);
    return out.good();
}

bool WorldPackage::loadFromDirectory(const std::string& directory) {
    core::SceneFile loadedScene;
    if (!loadedScene.loadFromFile(scenePath(directory))) return false;
    WorldMetadata loadedMetadata;
    if (!loadedMetadata.loadFromFile(metadataPath(directory))) return false;

    std::ifstream in(packageInfoPath(directory));
    if (!in.is_open()) return false;
    std::stringstream buffer;
    buffer << in.rdbuf();
    nlohmann::json packageInfo;
    try {
        packageInfo = nlohmann::json::parse(buffer.str());
        if (!packageInfo.contains("worldId") || !packageInfo.contains("version")) return false;
        std::string loadedWorldId = packageInfo.at("worldId").get<std::string>();
        std::string loadedVersion = packageInfo.at("version").get<std::string>();

        scene = std::move(loadedScene);
        metadata = std::move(loadedMetadata);
        worldId = std::move(loadedWorldId);
        version = std::move(loadedVersion);
        return true;
    } catch (const nlohmann::json::exception&) {
        return false;
    }
}

} // namespace engine::publishing
