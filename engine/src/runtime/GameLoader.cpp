#include "runtime/GameLoader.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "core/ProjectFile.hpp"
#include "core/SceneManager.hpp"

namespace engine::runtime {

namespace {

// Reads a whole real file into a string, or returns an empty string on
// any failure -- callers here treat "no entry script" as a real, honest
// no-op (see loadGame()'s own comment), not an error.
std::string readWholeFile(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) return {};
    std::ostringstream contents;
    contents << in.rdbuf();
    return contents.str();
}

} // namespace

bool loadGame(core::Application& app, const core::DiscoveredGame& game) {
    if (!game.parseSucceeded || game.manifest.launchKind != core::GameLaunchKind::ProjectPath) {
        std::fprintf(stderr, "GameLoader: \"%s\" is not a real ProjectPath game -- refusing to load\n",
                     game.manifest.name.c_str());
        return false;
    }

    std::filesystem::path gameDir = std::filesystem::path(game.manifestPath).parent_path();
    std::filesystem::path projectPath = gameDir / game.manifest.projectPath;

    core::ProjectFile project;
    if (!project.loadFromFile(projectPath.string())) {
        std::fprintf(stderr, "GameLoader: \"%s\"'s real project file \"%s\" failed to load\n",
                     game.manifest.name.c_str(), projectPath.string().c_str());
        return false;
    }
    if (project.scenePaths.empty() || project.activeSceneIndex < 0 ||
        static_cast<size_t>(project.activeSceneIndex) >= project.scenePaths.size()) {
        std::fprintf(stderr, "GameLoader: \"%s\"'s real project has no valid active scene\n",
                     game.manifest.name.c_str());
        return false;
    }
    std::filesystem::path scenePath = gameDir / project.scenePaths[static_cast<size_t>(project.activeSceneIndex)];

    // Real full reset -- see this function's own header comment for why
    // this exact ordering (Physics/Scripting down, then back up, all
    // before core::SceneManager::loadScene() clears+rebuilds the ECS) is
    // safe: this whole function runs synchronously within one call, with
    // no render/physics tick interleaved.
    core::Physics& physics = app.physics();
    core::Scripting& scripting = app.scripting();
    physics.shutdown();
    scripting.shutdown();
    if (!physics.initialize() || !scripting.initialize()) {
        std::fprintf(stderr, "GameLoader: \"%s\" failed to re-initialize Physics/Scripting during the real reset\n",
                     game.manifest.name.c_str());
        return false;
    }

    core::Renderer& renderer = app.renderer();
    core::SceneManager sceneManager;
    bool loaded = sceneManager.loadScene(scenePath.string(), app.ecs(), app.meshLibrary(), renderer.allocator(),
                                          renderer.device(), renderer.commandPool(), renderer.graphicsQueue(),
                                          app.camera(), &physics);
    if (!loaded) {
        std::fprintf(stderr, "GameLoader: \"%s\"'s real scene file \"%s\" failed to load\n",
                     game.manifest.name.c_str(), scenePath.string().c_str());
        return false;
    }

    // Real, honest convention (Kronos "Game Catalogue Overhaul", Phase
    // 2): Scripts/Main.lua sitting next to the project file, mirroring
    // studio::PluginManifest::entryScript's own "relative to the
    // manifest's own directory" convention. A game with no such file
    // (e.g. games/SkyGarden, a pure static scene) is real and valid --
    // no entry script is a real, honest no-op, not an error.
    std::filesystem::path scriptPath = gameDir / "Scripts" / "Main.lua";
    std::string scriptSource = readWholeFile(scriptPath.string());
    if (!scriptSource.empty()) {
        scripting.loadAndRun(game.manifest.name + "/Main", scriptSource);
    }

    return true;
}

} // namespace engine::runtime
