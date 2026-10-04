#pragma once

#include <optional>
#include <string>

#include "core/GameManifest.hpp"
#include "core/LocalGameDirectory.hpp"

namespace engine::core {
class KronosApi;
}

namespace engine::publishing {

// A playable catalog game is its whole folder: game.gamemanifest,
// project.project, scenes, Scripts/ and any assets. Hidden entries and
// symlinks are skipped. When the folder has no manifest, one built from
// `fallbackManifest` is added to the archive (the folder is not modified).
[[nodiscard]] bool writeGameFolderArchive(const std::string& gameDirectory, const core::GameManifest& fallbackManifest,
                                          const std::string& archivePath, std::string& error);

// Loads an extracted package, refusing anything that is not a ProjectPath
// game or whose project/scene paths would escape `directory`.
[[nodiscard]] std::optional<core::DiscoveredGame> loadPackagedGame(const std::string& directory, std::string& error);

// Writable per-user cache for downloaded packages: KRONOS_PACKAGE_CACHE_DIR,
// else %LOCALAPPDATA%/Kronos/packages, $XDG_CACHE_HOME or ~/.cache/kronos/packages,
// else the system temp directory.
[[nodiscard]] std::string packageCacheDirectory();

// What both a dedicated server and a joining client run for a catalog slug:
// the creator's uploaded package when there is one (downloaded once into
// `cacheDirectory`), otherwise a game shipped in `gamesDirectory` itself.
[[nodiscard]] std::optional<core::DiscoveredGame> resolveCatalogGame(core::KronosApi& api, const std::string& slug,
                                                                     const std::string& gamesDirectory,
                                                                     const std::string& cacheDirectory,
                                                                     std::string& error);

} // namespace engine::publishing
