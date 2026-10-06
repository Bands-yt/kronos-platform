#include "core/GameCatalogueAggregate.hpp"

#include <algorithm>
#include <iterator>

#include "core/LocalGameDirectory.hpp"
#include "core/MyGames.hpp"
#include "net/GamePlayLog.hpp"

namespace engine::core {

namespace {
std::vector<GameCatalogueEntry> buildEntries(const std::vector<DiscoveredGame>& discovered, bool mine,
                                             const net::GamePlayLog& playLog, int64_t nowUnixSeconds) {
    std::vector<GameCatalogueEntry> entries;
    entries.reserve(discovered.size());
    for (const auto& game : discovered) {
        if (!game.parseSucceeded) continue;

        GameCatalogueEntry entry;
        entry.manifestPath = game.manifestPath;
        entry.manifest = game.manifest;
        entry.mine = mine;
        std::vector<net::GamePlaySession> sessions = playLog.sessionsForGame(game.manifest.name);
        entry.stats = computeGamePlayStats(sessions, nowUnixSeconds);
        entry.qualityScore = computeQualityScore(game.manifest.effortScore, entry.stats);
        entry.launchCount = static_cast<int64_t>(sessions.size());
        for (const auto& session : sessions) {
            entry.lastPlayedUnixSeconds = std::max(entry.lastPlayedUnixSeconds, session.startUnixSeconds);
        }
        entries.push_back(std::move(entry));
    }
    return entries;
}
} // namespace

std::vector<GameCatalogueEntry> buildGameCatalogueEntries(const std::string& gamesDir, const std::string& playLogPath,
                                                            int64_t nowUnixSeconds) {
    std::vector<DiscoveredGame> discovered = scanLocalGameDirectory(gamesDir);
    if (discovered.empty()) return {};

    net::GamePlayLog playLog;
    (void)playLog.loadFromFile(playLogPath); // a real, honest missing/empty log just means zero real play data yet
    return buildEntries(discovered, false, playLog, nowUnixSeconds);
}

std::vector<GameCatalogueEntry> buildFullGameCatalogue(const std::string& gamesDir, const std::string& playLogPath,
                                                         int64_t nowUnixSeconds) {
    net::GamePlayLog playLog;
    (void)playLog.loadFromFile(playLogPath);
    std::vector<GameCatalogueEntry> entries = buildEntries(scanLocalGameDirectory(gamesDir), false, playLog, nowUnixSeconds);
    std::vector<GameCatalogueEntry> mine = buildEntries(scanMyGames(), true, playLog, nowUnixSeconds);
    entries.insert(entries.end(), std::make_move_iterator(mine.begin()), std::make_move_iterator(mine.end()));
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
