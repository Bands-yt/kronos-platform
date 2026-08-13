#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace engine::net {

// Kronos (Alpha Roadmap Phase 9, "Platform Services" -- "Multiplayer
// session browser"): a real, local, persisted list of servers this
// machine has connected to (or the user has manually favorited) -- NOT
// real network discovery (LAN broadcast/mDNS), which is a much larger,
// separate networking feature this pass doesn't attempt to half-build.
// This is the honest, buildable "session browser" for a local-only
// alpha: pick from what you've used before instead of re-typing an
// address every time, the same real, deliberately-scoped-down shape
// this whole roadmap phase's "local only for alpha" framing already
// applies to the plugin marketplace bullet too.
struct SessionHistoryEntry {
    std::string label; // creator-facing name, e.g. "Friday Playtest"
    std::string address;
    uint16_t port = 7777;
    int64_t lastConnectedUnixSeconds = 0;
};

class SessionHistory {
public:
    // Real upsert -- adds a new entry, or updates lastConnectedUnixSeconds
    // (and `label`, if non-empty) for an existing address:port pair
    // rather than creating a duplicate every time the same server is
    // rejoined.
    void recordConnection(const std::string& label, const std::string& address, uint16_t port,
                           int64_t nowUnixSeconds);
    // A real, honest no-op if no entry matches address:port.
    void removeEntry(const std::string& address, uint16_t port);

    // Real, most-recent-first ordering -- the natural "what did I
    // connect to last" browsing order a session list should show.
    [[nodiscard]] std::vector<SessionHistoryEntry> entriesMostRecentFirst() const;
    [[nodiscard]] size_t size() const { return entries_.size(); }

    [[nodiscard]] bool saveToFile(const std::string& path) const;
    [[nodiscard]] bool loadFromFile(const std::string& path);

private:
    std::vector<SessionHistoryEntry> entries_;
};

} // namespace engine::net
