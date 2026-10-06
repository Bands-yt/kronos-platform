#pragma once

#include <string>
#include <vector>

#include "core/LocalGameDirectory.hpp"

// "My Games": every project saved from Studio shows up in the Player's
// catalogue, the way Roblox lists your creations. New games go in
// myGamesDirectory(); projects saved anywhere else are remembered in a
// small registry file so they still appear.
namespace engine::core {

// KRONOS_MY_GAMES_DIR, else Documents/Kronos in the user's home folder.
[[nodiscard]] std::string myGamesDirectory();
// KRONOS_MY_GAMES_REGISTRY, else my_games.list in the user data folder.
[[nodiscard]] std::string myGamesRegistryPath();

// Creates a fresh folder for a game called `name` inside myGamesDirectory()
// (adding " 2", " 3"... if taken). Returns "" and sets `error` on failure.
[[nodiscard]] std::string createMyGameFolder(const std::string& name, std::string& error);

// Writes game.gamemanifest next to `projectPath` so the catalogue can list
// it. An existing manifest keeps its description, tags and colour; only its
// name and project file are updated.
bool writeGameManifestForProject(const std::string& projectPath, const std::string& gameName);

// Remembers a project saved outside myGamesDirectory().
void registerMyGame(const std::string& projectPath);

// Games in myGamesDirectory() plus registered projects that still exist,
// without duplicates.
[[nodiscard]] std::vector<DiscoveredGame> scanMyGames();

// Turns scene paths into ones relative to the project folder when they are
// inside it, so a project folder can be moved or loaded by the Player.
[[nodiscard]] std::vector<std::string> projectRelativeScenePaths(const std::string& projectPath,
                                                                   const std::vector<std::string>& scenePaths);
// The reverse, for opening: a relative path that exists next to the project
// wins over one relative to the working directory.
[[nodiscard]] std::string resolveProjectScenePath(const std::string& projectPath, const std::string& scenePath);

} // namespace engine::core
