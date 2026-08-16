#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/GameManifest.hpp"
#include "core/QualityScore.hpp"

namespace engine::core {

// Kronos ("Game Catalogue Overhaul"): one real local game, with its real
// catalogue metadata plus its real, freshly-computed QualityScore
// inputs -- the one shared real aggregation both engine_runtime's Game
// Catalogue UI (ranking Featured/genre rows) and studio::StudioApp's
// Hidden Gems eligibility check (core::selectHiddenGems(), which needs
// exactly this shape as core::HiddenGemCandidate) build on, so both real
// consumers agree on what "this game's QualityScore" means -- one real
// implementation, not two independently-drifting ones.
struct GameCatalogueEntry {
    std::string manifestPath; // same real path a core::DiscoveredGame carries -- what runtime::loadGame() needs to resolve the game's own directory
    GameManifest manifest;
    GamePlayStats stats;
    float qualityScore = 0.0f;
    int64_t launchCount = 0;
};

// Real, honest aggregation: scans `gamesDir` (core::scanLocalGameDirectory()),
// loads the real shared local play log at `playLogPath` (the same file
// runtime::GameLoader/RuntimeShell record real sessions into), and
// computes real per-game stats/QualityScore for every real,
// successfully-parsed game found. A manifest that fails to parse is
// real-skipped (not included) -- same "a caller can't rank what it can't
// real-read" reasoning core::scanLocalGameDirectory() itself already
// applies by exposing parseSucceeded. A real, honest empty result if
// `gamesDir` has no real games in it.
[[nodiscard]] std::vector<GameCatalogueEntry> buildGameCatalogueEntries(const std::string& gamesDir,
                                                                          const std::string& playLogPath,
                                                                          int64_t nowUnixSeconds);

// Kronos ("Moderation Architecture v2", "Catalogue Safety Integration"):
// real, pure "Catalogue hides unsafe games from minors" -- built directly
// on GameManifest.hpp's own isGameSafeToLaunchForAgeGroup() so the
// listing filter and RuntimeShell::selectGame()'s own launch-time
// defense-in-depth check share one real rule, not two. `viewerAgeGroup`
// is the real, self-declared, possibly-Unknown local viewer's own
// AgeGroup (core::LocalProfile::ageGroup) -- Unknown is treated
// conservatively, same as everywhere else this session.
[[nodiscard]] std::vector<GameCatalogueEntry> filterCatalogueEntriesForAgeGroup(
    const std::vector<GameCatalogueEntry>& entries, AgeGroup viewerAgeGroup);

} // namespace engine::core
