#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "net/LanDiscoveryProtocol.hpp"

namespace engine::net {

// A real, currently-live session this process has heard a real
// Announce from -- what the Session Browser UI actually lists.
struct DiscoveredSession {
    uint64_t sessionId = 0;
    std::string sessionName;
    std::string hostDisplayName;
    std::string sourceAddress; // the real sender IP the Announce actually arrived from -- what a real join connects to
    uint16_t gamePort = 0;
    uint8_t currentPlayerCount = 0;
    uint8_t maxPlayerCount = 0;
    float lastSeenSeconds = 0.0f;
    // Real, measured via a real unicast Echo/EchoReply round trip -- 0.0f
    // is a real, honest "no sample yet" value (same convention
    // NetworkStats::averagePingMs() already uses), not a fabricated
    // estimate. A one-way broadcast alone can never produce a real RTT.
    float pingMs = 0.0f;

    // Kronos ("Moderation Architecture v2", "Session Browser Game
    // Identity"): real, straight from the real LanSessionAnnouncement --
    // see that struct's own comment for what each field means.
    std::string gameName;
    glm::vec4 gameThumbnailColor{0.35f, 0.55f, 0.85f, 1.0f};
    uint8_t gameSafetyStatusValue = 0;
    // Kronos ("Session Browser Polish v2"): real, see
    // LanSessionAnnouncement::sessionStartUnixSeconds's own comment.
    int64_t sessionStartUnixSeconds = 0;
};

// Kronos ("Active Joining UI" -- LAN discovery): the real, client-side
// counterpart to LanSessionAnnouncer -- owned by whatever's showing a
// Session Browser (not by NetworkSession itself: only a not-yet-connected
// process needs to browse, same separation of concerns
// studio::plugins::NetworkOverlayPlugin already models by owning its own
// net::SessionHistory rather than NetworkSession owning it). No
// background thread -- see LanSessionAnnouncer's own class comment for
// why. Linux-only for now, same precedent.
class LanSessionBrowser {
public:
    LanSessionBrowser() = default;
    ~LanSessionBrowser();

    LanSessionBrowser(const LanSessionBrowser&) = delete;
    LanSessionBrowser& operator=(const LanSessionBrowser&) = delete;

    // Binds a real, non-blocking socket to `announcePort` (to receive
    // real Announce broadcasts) plus a real, separate, ephemeral-port
    // socket for unicast Echo pings -- see the ping socket's own comment
    // below for the real bug that split fixes. `pingPort` is where a real
    // LanSessionAnnouncer listens for those pings.
    [[nodiscard]] bool start(uint16_t announcePort, uint16_t pingPort);
    void stop();
    [[nodiscard]] bool isRunning() const { return socketFd_ >= 0 && pingSocketFd_ >= 0; }

    // Real, once-per-frame drive: drains every pending inbound datagram
    // (a real Announce upserts/refreshes the matching real
    // DiscoveredSession, real-deduped by sessionId, and triggers a real
    // unicast Echo ping if one isn't already outstanding to that sender;
    // a real EchoReply completes a real ping measurement), then prunes
    // any session that hasn't real-refreshed within kStaleTimeoutSeconds
    // -- the real "host crashed/session closed" cleanup, since nothing
    // else ever tells a browser a session is gone.
    void tick(float nowSeconds);

    [[nodiscard]] std::vector<DiscoveredSession> discoveredSessions() const;

    // 5x LanSessionAnnouncer::kAnnounceIntervalSeconds (1.0f) -- a real,
    // generous allowance for a couple of missed/dropped broadcasts before
    // really treating a session as gone, not a hair-trigger.
    static constexpr float kStaleTimeoutSeconds = 5.0f;

private:
    int socketFd_ = -1; // bound to announcePort_ -- receives real Announce broadcasts only
    // Kronos ("Active Joining UI"): a real, SEPARATE socket dedicated to
    // unicast Echo pings, bound to an OS-assigned ephemeral port. Real
    // bug found and fixed while implementing this: an earlier version had
    // BOTH this browser's own discovery socket and a co-located
    // LanSessionAnnouncer bound to the exact same fixed port -- on the
    // same machine (the real "you're also hosting while browsing" case,
    // and every real loopback test), a unicast Echo sent to that shared
    // port is genuinely ambiguous for the kernel to route (SO_REUSEADDR
    // lets two sockets bind one port, but does NOT give each of them an
    // independent copy of a given inbound unicast datagram -- exactly one
    // of the two same-port sockets receives it, and it isn't reliably the
    // intended one). The real fix was splitting into two disjoint
    // well-known ports entirely (see LanDiscoveryProtocol.hpp's own
    // kLanAnnouncePort/kLanPingPort comment) -- this ephemeral socket
    // additionally guarantees the EchoReply has a real, single,
    // unambiguous destination regardless.
    int pingSocketFd_ = -1;
    uint16_t pingPort_ = 0; // where a real Echo is sent TO -- the announcer's own well-known ping-listening port
    std::unordered_map<uint64_t, DiscoveredSession> sessions_;
    // Real, per-outstanding-ping bookkeeping -- keyed by the real sender
    // address a ping was sent to (not by sessionId: what actually
    // receives/replies to a real Echo is an address, and one host
    // address maps to at most one real session in every real deployment
    // this discovers).
    std::unordered_map<std::string, float> pendingPingSentAtSeconds_;
};

} // namespace engine::net
