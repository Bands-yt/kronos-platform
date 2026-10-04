#include "publishing/GamePackage.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

#include "core/KronosApi.hpp"
#include "core/ProjectFile.hpp"
#include "publishing/AssetStreamingClient.hpp"
#include "publishing/PackageArchive.hpp"

namespace engine::publishing {

namespace {
constexpr const char* kManifestName = "game.gamemanifest";

bool readFileBytes(const std::filesystem::path& path, std::vector<uint8_t>& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}
} // namespace

bool writeGameFolderArchive(const std::string& gameDirectory, const core::GameManifest& fallbackManifest,
                            const std::string& archivePath, std::string& error) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::is_directory(gameDirectory, ec)) {
        error = "The project folder \"" + gameDirectory + "\" does not exist.";
        return false;
    }

    std::vector<ArchiveFileEntry> files;
    bool hasManifest = false;
    fs::recursive_directory_iterator it(gameDirectory, fs::directory_options::none, ec);
    for (; !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        const fs::directory_entry& entry = *it;
        std::string filename = entry.path().filename().string();
        if (!filename.empty() && filename.front() == '.') {
            if (entry.is_directory()) it.disable_recursion_pending();
            continue;
        }
        if (entry.is_symlink() || !entry.is_regular_file()) continue;

        std::string relative = fs::relative(entry.path(), gameDirectory).generic_string();
        if (!isSafeRelativePath(relative)) continue;
        ArchiveFileEntry file{relative, {}};
        if (!readFileBytes(entry.path(), file.data)) {
            error = "Could not read \"" + relative + "\".";
            return false;
        }
        if (relative == kManifestName) hasManifest = true;
        files.push_back(std::move(file));
    }
    if (ec) {
        error = "Could not scan the project folder: " + ec.message();
        return false;
    }

    core::GameManifest manifest = fallbackManifest;
    if (hasManifest && !manifest.loadFromFile((fs::path(gameDirectory) / kManifestName).string())) {
        error = "The project's game.gamemanifest could not be parsed.";
        return false;
    }
    if (manifest.launchKind != core::GameLaunchKind::ProjectPath) {
        error = "Only scene-based games can be published to the catalog.";
        return false;
    }
    if (!fs::is_regular_file(fs::path(gameDirectory) / manifest.projectPath)) {
        error = "The project file \"" + manifest.projectPath + "\" is missing -- save the project first.";
        return false;
    }

    if (!hasManifest) {
        std::string manifestPath = archivePath + ".manifest";
        ArchiveFileEntry file{kManifestName, {}};
        bool ok = manifest.saveToFile(manifestPath) && readFileBytes(manifestPath, file.data);
        fs::remove(manifestPath, ec);
        if (!ok) {
            error = "Could not write the game manifest.";
            return false;
        }
        files.push_back(std::move(file));
    }

    if (!writeArchive(archivePath, files)) {
        error = "Could not write the package archive.";
        return false;
    }
    return true;
}

std::optional<core::DiscoveredGame> loadPackagedGame(const std::string& directory, std::string& error) {
    namespace fs = std::filesystem;
    core::DiscoveredGame game;
    game.manifestPath = (fs::path(directory) / kManifestName).string();
    if (!game.manifest.loadFromFile(game.manifestPath)) {
        error = "The package has no readable game.gamemanifest.";
        return std::nullopt;
    }
    if (game.manifest.launchKind != core::GameLaunchKind::ProjectPath ||
        !isSafeRelativePath(fs::path(game.manifest.projectPath).lexically_normal().generic_string())) {
        error = "The package's manifest does not point at a project inside the package.";
        return std::nullopt;
    }

    core::ProjectFile project;
    if (!project.loadFromFile((fs::path(directory) / game.manifest.projectPath).string())) {
        error = "The package's project file could not be loaded.";
        return std::nullopt;
    }
    for (const std::string& scene : project.scenePaths) {
        if (!isSafeRelativePath(fs::path(scene).lexically_normal().generic_string())) {
            error = "The package references a scene outside itself.";
            return std::nullopt;
        }
    }

    game.parseSucceeded = true;
    return game;
}

std::string packageCacheDirectory() {
    namespace fs = std::filesystem;
    auto env = [](const char* name) {
        const char* value = std::getenv(name);
        return value != nullptr ? std::string(value) : std::string();
    };
    if (std::string dir = env("KRONOS_PACKAGE_CACHE_DIR"); !dir.empty()) return dir;
#ifdef _WIN32
    if (std::string dir = env("LOCALAPPDATA"); !dir.empty()) return (fs::path(dir) / "Kronos" / "packages").string();
#else
    if (std::string dir = env("XDG_CACHE_HOME"); !dir.empty()) return (fs::path(dir) / "kronos" / "packages").string();
    if (std::string dir = env("HOME"); !dir.empty()) return (fs::path(dir) / ".cache" / "kronos" / "packages").string();
#endif
    std::error_code ec;
    return (fs::temp_directory_path(ec) / "kronos_packages").string();
}

std::optional<core::DiscoveredGame> resolveCatalogGame(core::KronosApi& api, const std::string& slug,
                                                       const std::string& gamesDirectory,
                                                       const std::string& cacheDirectory, std::string& error) {
    PackageFetchResult fetched = fetchGamePackage(api, slug, cacheDirectory);
    if (fetched.success) {
        std::optional<core::DiscoveredGame> game = loadPackagedGame(fetched.extractedDirectory, error);
        if (game.has_value()) return game;
    } else {
        error = fetched.error;
    }

    std::optional<core::DiscoveredGame> local = core::findGameBySlug(gamesDirectory, slug);
    if (local.has_value()) {
        error.clear();
        return local;
    }
    if (error.empty()) error = "No playable copy of \"" + slug + "\" was found.";
    return std::nullopt;
}

} // namespace engine::publishing
