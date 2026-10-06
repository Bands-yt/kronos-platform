#include "core/MyGames.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <system_error>

namespace engine::core {
namespace fs = std::filesystem;

namespace {
std::string env(const char* name) {
    const char* value = std::getenv(name);
    return value ? std::string(value) : std::string();
}

fs::path homeDirectory() {
#if defined(_WIN32)
    if (std::string dir = env("USERPROFILE"); !dir.empty()) return dir;
#endif
    return env("HOME");
}

std::string canonicalKey(const fs::path& path) {
    std::error_code ec;
    fs::path canonical = fs::weakly_canonical(path, ec);
    return (ec ? path : canonical).lexically_normal().string();
}

std::vector<std::string> readRegistry() {
    std::vector<std::string> paths;
    std::ifstream in(myGamesRegistryPath());
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) paths.push_back(line);
    }
    return paths;
}

glm::vec4 colorForName(const std::string& name) {
    static const glm::vec4 kPalette[] = {
        {0.93f, 0.36f, 0.33f, 1.0f}, {0.98f, 0.62f, 0.25f, 1.0f}, {0.96f, 0.80f, 0.27f, 1.0f},
        {0.35f, 0.78f, 0.45f, 1.0f}, {0.24f, 0.67f, 0.89f, 1.0f}, {0.45f, 0.45f, 0.92f, 1.0f},
        {0.72f, 0.42f, 0.88f, 1.0f}, {0.92f, 0.43f, 0.68f, 1.0f},
    };
    return kPalette[std::hash<std::string>{}(name) % std::size(kPalette)];
}

std::string folderSafeName(const std::string& name) {
    std::string out;
    for (char c : name) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') continue;
        if (static_cast<unsigned char>(c) < 32) continue;
        out.push_back(c);
    }
    while (!out.empty() && (out.back() == ' ' || out.back() == '.')) out.pop_back();
    while (!out.empty() && out.front() == ' ') out.erase(out.begin());
    return out.empty() ? std::string("My Game") : out;
}
} // namespace

std::string myGamesDirectory() {
    if (std::string dir = env("KRONOS_MY_GAMES_DIR"); !dir.empty()) return dir;
    fs::path home = homeDirectory();
    if (home.empty()) return (fs::current_path() / "My Games").string();
    return (home / "Documents" / "Kronos").string();
}

std::string myGamesRegistryPath() {
    if (std::string path = env("KRONOS_MY_GAMES_REGISTRY"); !path.empty()) return path;
#if defined(_WIN32)
    if (std::string dir = env("LOCALAPPDATA"); !dir.empty()) return (fs::path(dir) / "Kronos" / "my_games.list").string();
#else
    if (std::string dir = env("XDG_DATA_HOME"); !dir.empty()) return (fs::path(dir) / "kronos" / "my_games.list").string();
    if (std::string dir = env("HOME"); !dir.empty())
        return (fs::path(dir) / ".local" / "share" / "kronos" / "my_games.list").string();
#endif
    return "my_games.list";
}

std::string createMyGameFolder(const std::string& name, std::string& error) {
    const fs::path root = myGamesDirectory();
    const std::string base = folderSafeName(name);
    std::error_code ec;
    for (int n = 1; n < 1000; ++n) {
        fs::path candidate = root / (n == 1 ? base : base + " " + std::to_string(n));
        if (fs::exists(candidate, ec)) continue;
        if (!fs::create_directories(candidate, ec)) {
            error = "Couldn't create " + candidate.string() + ": " + ec.message();
            return {};
        }
        return candidate.string();
    }
    error = "Too many games called \"" + base + "\"";
    return {};
}

bool writeGameManifestForProject(const std::string& projectPath, const std::string& gameName) {
    const fs::path project(projectPath);
    const fs::path manifestPath = project.parent_path() / "game.gamemanifest";
    GameManifest manifest;
    if (!manifest.loadFromFile(manifestPath.string())) {
        manifest = GameManifest{};
        manifest.description = "Made in Kronos Studio.";
        manifest.thumbnailColor = colorForName(gameName);
    }
    manifest.name = gameName;
    manifest.launchKind = GameLaunchKind::ProjectPath;
    manifest.projectPath = project.filename().string();
    return manifest.saveToFile(manifestPath.string());
}

void registerMyGame(const std::string& projectPath) {
    const std::string key = canonicalKey(projectPath);
    std::vector<std::string> paths = readRegistry();
    for (const std::string& existing : paths) {
        if (canonicalKey(existing) == key) return;
    }
    const fs::path registry = myGamesRegistryPath();
    std::error_code ec;
    if (registry.has_parent_path()) fs::create_directories(registry.parent_path(), ec);
    std::ofstream out(registry, std::ios::app);
    out << key << "\n";
}

std::vector<DiscoveredGame> scanMyGames() {
    std::vector<DiscoveredGame> games = scanLocalGameDirectory(myGamesDirectory());
    std::vector<std::string> seen;
    for (const DiscoveredGame& game : games) seen.push_back(canonicalKey(game.manifestPath));

    std::error_code ec;
    for (const std::string& projectPath : readRegistry()) {
        fs::path manifestPath = fs::path(projectPath).parent_path() / "game.gamemanifest";
        if (!fs::is_regular_file(projectPath, ec) || !fs::is_regular_file(manifestPath, ec)) continue;
        std::string key = canonicalKey(manifestPath);
        if (std::find(seen.begin(), seen.end(), key) != seen.end()) continue;
        seen.push_back(key);
        DiscoveredGame game;
        game.manifestPath = manifestPath.string();
        game.parseSucceeded = game.manifest.loadFromFile(game.manifestPath);
        games.push_back(std::move(game));
    }
    return games;
}

std::vector<std::string> projectRelativeScenePaths(const std::string& projectPath,
                                                     const std::vector<std::string>& scenePaths) {
    const fs::path projectDir = fs::absolute(fs::path(projectPath)).parent_path().lexically_normal();
    std::vector<std::string> out;
    out.reserve(scenePaths.size());
    for (const std::string& scenePath : scenePaths) {
        const fs::path absolute = fs::absolute(fs::path(scenePath)).lexically_normal();
        const fs::path relative = absolute.lexically_relative(projectDir);
        const bool inside = !relative.empty() && *relative.begin() != "..";
        out.push_back(inside ? relative.generic_string() : absolute.string());
    }
    return out;
}

std::string resolveProjectScenePath(const std::string& projectPath, const std::string& scenePath) {
    const fs::path scene(scenePath);
    if (scene.is_absolute()) return scenePath;
    std::error_code ec;
    const fs::path nextToProject = fs::path(projectPath).parent_path() / scene;
    if (fs::exists(nextToProject, ec)) return nextToProject.lexically_normal().string();
    return scenePath;
}

} // namespace engine::core
