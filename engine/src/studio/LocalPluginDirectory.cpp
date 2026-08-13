#include "studio/LocalPluginDirectory.hpp"

#include <filesystem>
#include <system_error>

namespace engine::studio {

std::vector<DiscoveredPlugin> scanLocalPluginDirectory(const std::string& directoryPath) {
    std::vector<DiscoveredPlugin> discovered;

    std::error_code ec;
    if (!std::filesystem::exists(directoryPath, ec) || !std::filesystem::is_directory(directoryPath, ec)) {
        return discovered; // real, honest empty result -- see this function's own doc comment
    }

    for (const auto& entry : std::filesystem::directory_iterator(directoryPath, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".manifest") continue;

        DiscoveredPlugin discoveredPlugin;
        discoveredPlugin.manifestPath = entry.path().string();
        discoveredPlugin.parseSucceeded = discoveredPlugin.manifest.loadFromFile(discoveredPlugin.manifestPath);
        discovered.push_back(std::move(discoveredPlugin));
    }

    return discovered;
}

} // namespace engine::studio
