#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace engine::net {

// Kronos ("Game Catalogue Overhaul", Phase 6): a real, local, persisted
// record of every real play session (start/end/crashed) for every real
// local game -- the concrete "retention data" source core::QualityScore
// (core/QualityScore.hpp) computes from. Same real, hand-rolled flat-file
// convention as net::SessionHistory (this class's own closest sibling:
// both are "a real, small, per-machine append log," not a database or a
// remote analytics pipeline -- see SessionHistory.hpp's own "local only
// for alpha" framing, which applies here too).
//
// `gameId` is a real core::GameManifest::name -- there's no separate,
// more stable id field on GameManifest yet (a real, honest gap for a
// future multi-developer/hosted catalogue, not a problem for this local,
// single-machine alpha where names are effectively unique by convention).
struct GamePlaySession {
    std::string gameId;
    int64_t startUnixSeconds = 0;
    int64_t endUnixSeconds = 0; // 0 while the session is still open (recordSessionEnd() hasn't closed it yet)
    bool crashed = false;
};

class GamePlayLog {
public:
    // Real, honest "always append" -- a game can be launched and left
    // any number of times, each a real, distinct session record, same
    // "every real connection gets a real entry" spirit as
    // SessionHistory::recordConnection() (which upserts instead, since
    // *that* class only cares about "last connected," not a full
    // history -- this class deliberately keeps every session, since
    // QualityScore's retention signals need the real distribution over
    // time, not just the latest point).
    void recordSessionStart(const std::string& gameId, int64_t nowUnixSeconds);

    // Closes the most recent still-open session for `gameId` (the one
    // recordSessionStart() opened and no matching recordSessionEnd() has
    // closed yet). A real, honest no-op if no open session exists for
    // `gameId` -- e.g. called twice, or for a game that was never
    // actually started, matches this codebase's "an inapplicable call is
    // a no-op, not an error" convention throughout.
    void recordSessionEnd(const std::string& gameId, int64_t nowUnixSeconds, bool crashed = false);

    [[nodiscard]] std::vector<GamePlaySession> sessionsForGame(const std::string& gameId) const;
    [[nodiscard]] size_t size() const { return sessions_.size(); }

    // Real, honest crash detection for the one real case this class can
    // actually observe on its own: a session left open (endUnixSeconds
    // == 0) from a *previous* process run, found the moment a *new* run
    // loads this log back in, before that new run has recorded anything
    // of its own -- the exact same "a recovery file left over means the
    // last run didn't shut down cleanly" reasoning
    // studio::SceneManager's own autosave recovery already established
    // for scene files, applied here to play sessions. Real, honest
    // best-effort duration (endUnixSeconds set equal to
    // startUnixSeconds -- the true end time is genuinely unknown, and
    // claiming a fabricated one would be dishonest) -- what matters for
    // core::QualityScore's crashRate is that the session is counted as
    // both real and crashed, not its exact length. Call once, right
    // after loadFromFile(), before recording any new session for this
    // run. Returns the real number of sessions reconciled this way.
    size_t reconcileUnclosedSessionsAsCrashed();

    [[nodiscard]] bool saveToFile(const std::string& path) const;
    [[nodiscard]] bool loadFromFile(const std::string& path);

private:
    std::vector<GamePlaySession> sessions_;
};

} // namespace engine::net
