#include "core/LocalGameDirectory.hpp"

#include <filesystem>
#include <system_error>

namespace engine::core {

std::vector<DiscoveredGame> scanLocalGameDirectory(const std::string& directoryPath) {
    std::vector<DiscoveredGame> discovered;

    std::error_code ec;
    if (!std::filesystem::exists(directoryPath, ec) || !std::filesystem::is_directory(directoryPath, ec)) {
        return discovered; // real, honest empty result -- see this function's own doc comment
    }

    for (const auto& entry : std::filesystem::directory_iterator(directoryPath, ec)) {
        if (ec) break;
        if (!entry.is_directory()) continue;

        std::filesystem::path manifestPath = entry.path() / "game.gamemanifest";
        std::error_code manifestEc;
        if (!std::filesystem::is_regular_file(manifestPath, manifestEc)) continue;

        DiscoveredGame discoveredGame;
        discoveredGame.manifestPath = manifestPath.string();
        discoveredGame.parseSucceeded = discoveredGame.manifest.loadFromFile(discoveredGame.manifestPath);
        discovered.push_back(std::move(discoveredGame));
    }

    return discovered;
}

} // namespace engine::core
