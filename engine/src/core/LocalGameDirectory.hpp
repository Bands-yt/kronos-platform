#pragma once

#include <string>
#include <vector>

#include "core/GameManifest.hpp"

namespace engine::core {

// Kronos ("Game Catalogue Overhaul", Phase 0): the same real, local-only
// discovery philosophy studio::scanLocalPluginDirectory() already
// established for plugins (see that function's own comment) -- applied
// here to games, but scanning one level of *subdirectories* rather than
// flat manifest files: a plugin is just a manifest + one script sitting
// side by side, but a game also needs its own project.project/*.scene/
// Scripts/ subtree alongside its manifest, so each game gets its own real
// folder (`<directoryPath>/<GameFolder>/game.gamemanifest`) rather than
// living flat next to every other game's files. Lives in core:: (not
// studio::) since both engine_runtime (the Game Catalogue itself) and
// studio (Phase 7's Hidden Gems notification) need it.
struct DiscoveredGame {
    std::string manifestPath;
    GameManifest manifest; // only meaningful when parseSucceeded is true
    bool parseSucceeded = false;
};

// A real, honest empty result (not an error) if `directoryPath` doesn't
// exist or has no `*.gamemanifest` files -- same "missing input is a real
// zero-result answer, not a thrown exception" convention
// scanLocalPluginDirectory() already uses. A manifest file that fails to
// parse is still included, with parseSucceeded = false, so a caller can
// show *which* file is broken rather than silently skipping it.
[[nodiscard]] std::vector<DiscoveredGame> scanLocalGameDirectory(const std::string& directoryPath);

} // namespace engine::core
