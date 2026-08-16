#include "core/GameCatalogueAggregate.hpp"

#include "core/LocalGameDirectory.hpp"
#include "net/GamePlayLog.hpp"

namespace engine::core {

std::vector<GameCatalogueEntry> buildGameCatalogueEntries(const std::string& gamesDir, const std::string& playLogPath,
                                                            int64_t nowUnixSeconds) {
    std::vector<GameCatalogueEntry> entries;

    std::vector<DiscoveredGame> discovered = scanLocalGameDirectory(gamesDir);
    if (discovered.empty()) return entries;

    net::GamePlayLog playLog;
    (void)playLog.loadFromFile(playLogPath); // a real, honest missing/empty log just means zero real play data yet

    entries.reserve(discovered.size());
    for (const auto& game : discovered) {
        if (!game.parseSucceeded) continue;

        GameCatalogueEntry entry;
        entry.manifestPath = game.manifestPath;
        entry.manifest = game.manifest;
        std::vector<net::GamePlaySession> sessions = playLog.sessionsForGame(game.manifest.name);
        entry.stats = computeGamePlayStats(sessions, nowUnixSeconds);
        entry.qualityScore = computeQualityScore(game.manifest.effortScore, entry.stats);
        entry.launchCount = static_cast<int64_t>(sessions.size());
        entries.push_back(std::move(entry));
    }

    return entries;
}

std::vector<GameCatalogueEntry> filterCatalogueEntriesForAgeGroup(const std::vector<GameCatalogueEntry>& entries,
                                                                     AgeGroup viewerAgeGroup) {
    std::vector<GameCatalogueEntry> filtered;
    filtered.reserve(entries.size());
    for (const GameCatalogueEntry& entry : entries) {
        if (isGameSafeToLaunchForAgeGroup(entry.manifest.safetyStatus, viewerAgeGroup)) {
            filtered.push_back(entry);
        }
    }
    return filtered;
}

} // namespace engine::core
