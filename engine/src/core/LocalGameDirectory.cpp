#include "core/LocalGameDirectory.hpp"

#include <cctype>
#include <filesystem>
#include <system_error>

namespace engine::core {

std::string slugifyGameName(const std::string& name) {
    std::string result;
    result.reserve(name.size());
    bool inDashRun = false;
    for (unsigned char c : name) {
        char lower = static_cast<char>(std::tolower(c));
        bool isAlnum = (lower >= 'a' && lower <= 'z') || (lower >= '0' && lower <= '9');
        if (isAlnum) {
            result.push_back(lower);
            inDashRun = false;
        } else if (!inDashRun) {
            result.push_back('-');
            inDashRun = true;
        }
    }
    size_t start = result.find_first_not_of('-');
    if (start == std::string::npos) return {};
    size_t end = result.find_last_not_of('-');
    return result.substr(start, end - start + 1);
}

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

std::optional<DiscoveredGame> findGameBySlug(const std::string& directoryPath, const std::string& slug) {
    for (DiscoveredGame& game : scanLocalGameDirectory(directoryPath)) {
        if (game.parseSucceeded && slugifyGameName(game.manifest.name) == slug) {
            return game;
        }
    }
    return std::nullopt;
}

} // namespace engine::core
