#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "core/LocalProfile.hpp"

namespace engine::core {

// Which of two real ways a Game Catalogue entry can actually be launched
// (see LocalGameDirectory.hpp's own comment for the full "why two kinds"
// story): ProjectPath loads a real core::ProjectFile/SceneFile through
// runtime::loadGame() (Kronos "Game Catalogue Overhaul", Phase 2) --
// genuinely swappable, independently-authored content. CliFlag relaunches
// engine_runtime itself with a fixed CLI flag (core::launchProcess(),
// Phase 3) -- the honest way to front the still-hardcoded-in-C++ rich
// modes (TNT Wars/Mining Sim/House Demo) without migrating their bespoke
// gameplay systems into scene data this pass.
enum class GameLaunchKind { ProjectPath, CliFlag };

[[nodiscard]] inline const char* gameLaunchKindName(GameLaunchKind kind) {
    switch (kind) {
        case GameLaunchKind::ProjectPath: return "ProjectPath";
        case GameLaunchKind::CliFlag: return "CliFlag";
    }
    return "ProjectPath";
}

// Kronos ("Moderation Architecture v2", "Catalogue Safety Integration"):
// a real, manually-authored (by a creator or moderator, e.g. via a real,
// future Studio review action) flag on a game's own catalogue entry --
// there is no automated content-understanding system in this codebase
// that could set this itself (matches every other classifier in
// safety::'s own honesty convention: real heuristics classify *messages
// and uploads*, nothing here scans a whole game's world/scripts for
// safety today). `UnderReview` is a real, non-blocking "flagged, not yet
// confirmed either way" state; `Unsafe` is the real, confirmed-unsafe
// state that actually changes catalogue/launch behavior.
enum class GameSafetyStatus { Safe, UnderReview, Unsafe };

[[nodiscard]] inline const char* gameSafetyStatusName(GameSafetyStatus status) {
    switch (status) {
        case GameSafetyStatus::Safe: return "Safe";
        case GameSafetyStatus::UnderReview: return "UnderReview";
        case GameSafetyStatus::Unsafe: return "Unsafe";
    }
    return "Safe";
}

// The one real, shared rule "Catalogue hides unsafe games from minors"
// and "blocks launching unsafe games in minor mode" both reduce to --
// kept as a single pure function so the Catalogue's own listing filter
// (core::filterCatalogueEntriesForAgeGroup(), GameCatalogueAggregate.hpp)
// and RuntimeShell::selectGame()'s real launch-time defense-in-depth
// check can never drift apart into two different real rules. An
// `UnderReview` game is real, honestly still playable -- it's flagged
// for a human to look at, not confirmed unsafe -- so only `Unsafe`
// itself, and only for a non-Adult viewer (AgeGroup::Unknown treated
// conservatively, same "protect minors by default" convention as every
// other real age-gate this session), is ever blocked.
[[nodiscard]] inline bool isGameSafeToLaunchForAgeGroup(GameSafetyStatus status, AgeGroup viewerAgeGroup) {
    return status != GameSafetyStatus::Unsafe || viewerAgeGroup == AgeGroup::Adult;
}

// A local game's real catalogue metadata -- the same hand-rolled, forward-
// compatible "KEY value per line, END terminator" text format
// studio::PluginManifest already established for plugin packaging, applied
// here to game packaging. No JSON/serialization library pulled in for a
// data shape this small, same reasoning as PluginManifest.
//
// `thumbnailColor` is a real, honest answer to "what's the thumbnail" --
// no image-thumbnail pipeline exists anywhere in this codebase yet
// (studio::plugins::CataloguePanel's own item cards are flat color
// swatches for the exact same reason, see its class comment); a real
// image asset is a separate, later feature, not faked here.
//
// `effortScore` (content depth/update frequency) has no automatic data
// source anywhere in this codebase -- it's a real, manually-authored
// field, a stated scope gap (see core/QualityScore.hpp), not an ML-derived
// value yet, matching the user's own "ML-ready" (not "ML now") framing.
struct GameManifest {
    std::string name = "New Game";
    std::string description;
    std::vector<std::string> genreTags;
    glm::vec4 thumbnailColor{0.35f, 0.55f, 0.85f, 1.0f};
    float effortScore = 0.5f;

    GameLaunchKind launchKind = GameLaunchKind::ProjectPath;
    std::string projectPath = "project.project"; // relative to the manifest file's own directory; used when launchKind == ProjectPath
    std::string cliFlag;                         // e.g. "--tntwars"; used when launchKind == CliFlag

    // Kronos ("Moderation Architecture v2", "Catalogue Safety
    // Integration"): see GameSafetyStatus's own comment. `unsafeReason`
    // is real, human-authored free text (a moderator's own stated
    // reason), only meaningful when safetyStatus != Safe -- same
    // "meaningful only alongside its own flag" convention
    // AssetSafetyFinding::description already establishes.
    GameSafetyStatus safetyStatus = GameSafetyStatus::Safe;
    std::string unsafeReason;

    [[nodiscard]] bool saveToFile(const std::string& path) const;
    [[nodiscard]] bool loadFromFile(const std::string& path);
};

} // namespace engine::core
