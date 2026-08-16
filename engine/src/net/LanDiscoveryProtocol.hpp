#pragma once

#include <cstdint>
#include <string>

#include <glm/glm.hpp>

namespace engine::net {

class ByteWriter;
class ByteReader;

// Kronos ("Active Joining UI" -- LAN discovery): the real, shared wire
// format for LAN session discovery -- a small, separate protocol from
// NetworkSession's own game-traffic wire format (net/NetworkSession.cpp's
// anonymous-namespace WireMessageType enum), carried over its own real,
// separate UDP socket/port (see LanSessionAnnouncer/LanSessionBrowser),
// not through ENet at all. A not-yet-connected process has no ENet peer
// to send/receive game traffic through yet -- that's the entire reason
// this exists as a real, independent mechanism.

// Every real LAN discovery datagram starts with a magic marker + this
// kind byte (see writeLanDiscoveryHeader()/readLanDiscoveryHeader()) --
// a real sanity check against stray, unrelated broadcast traffic
// happening to land on the same UDP port, not just an assumption nothing
// else ever will.
enum class LanMessageKind : uint8_t {
    Invalid = 0, // real, honest "not a real LAN discovery datagram" sentinel -- never sent, only ever returned
    Announce = 1,
    Echo = 2,
    EchoReply = 3,
};

// Two real, fixed, documented, and deliberately DISJOINT UDP ports.
// Real bug found while implementing: a single shared discovery port,
// bound by BOTH a LanSessionAnnouncer and a LanSessionBrowser, is
// ambiguous the moment they run on the same machine (a host that's also
// browsing -- a genuinely real scenario, not just a test artifact) --
// SO_REUSEADDR lets two sockets bind the same port, but does NOT give
// each of them an independent copy of inbound unicast traffic; the
// kernel delivers a given datagram to only one of them, and there's no
// portable way to control which. Splitting into two disjoint ports
// removes the ambiguity by construction: nothing ever needs to bind both.
//
// kLanAnnouncePort: a LanSessionAnnouncer sends its broadcasts here; a
// LanSessionBrowser binds here to receive them.
// kLanPingPort: a LanSessionAnnouncer binds here to receive real unicast
// Echo pings and reply; a LanSessionBrowser sends its pings here (from
// its own, separate, ephemeral-port socket -- see LanSessionBrowser's
// own class comment).
//
// Both are distinct from any real NetworkSession::Config::port (the ENet
// game-traffic port) -- discovery and gameplay traffic never share a
// socket either.
inline constexpr uint16_t kLanAnnouncePort = 45677;
inline constexpr uint16_t kLanPingPort = 45678;

void writeLanDiscoveryHeader(ByteWriter& writer, LanMessageKind kind);
// Real, honest LanMessageKind::Invalid on a magic-marker mismatch, a
// read error (too-short datagram), or an unrecognized kind byte -- the
// caller's real, correct response to any of those is "silently ignore
// this datagram", not to attempt to parse a payload that doesn't exist.
[[nodiscard]] LanMessageKind readLanDiscoveryHeader(ByteReader& reader);

// A real, live, joinable session's own real, current announcement --
// what a LanSessionAnnouncer actually broadcasts and a LanSessionBrowser
// actually parses into its own real, per-session DiscoveredSession
// (net/LanSessionBrowser.hpp).
struct LanSessionAnnouncement {
    uint32_t protocolVersion = 0; // compared against kNetworkProtocolVersion -- a mismatched session can't really be joined anyway
    uint64_t sessionId = 0;
    std::string sessionName;
    std::string hostDisplayName;
    uint16_t gamePort = 0; // the real ENet port to actually connect to -- distinct from kLanDiscoveryPort
    uint8_t currentPlayerCount = 0;
    uint8_t maxPlayerCount = 0;

    // Kronos ("Moderation Architecture v2", "Session Browser Game
    // Identity"): real "which game is this session actually running" --
    // `gameName` doubles as the real identity key this codebase already
    // uses everywhere else (core::GameManifest::name -- the same string
    // net::GamePlayLog/core::HiddenGemsSelector already key on, see
    // GameCatalogueAggregate.hpp's own comment), not a separate numeric
    // gameId scheme this codebase has no other use for. `gameThumbnailColor`
    // mirrors GameManifest's own real, honest "no image pipeline yet, a
    // flat color swatch is the real answer" convention.
    // `gameSafetyStatusValue` is a real core::GameSafetyStatus, stored as
    // a raw uint8_t here (net/ doesn't otherwise depend on core::) --
    // callers cast via static_cast<core::GameSafetyStatus>, same
    // "storage is a plain integer, the real enum lives in core::" pattern
    // AgeGroup gets nowhere in this wire format (it's sent as a raw u8
    // too, see NetworkSession's own JoinRequest handling).
    std::string gameName;
    glm::vec4 gameThumbnailColor{0.35f, 0.55f, 0.85f, 1.0f};
    uint8_t gameSafetyStatusValue = 0; // core::GameSafetyStatus::Safe's own underlying value

    // Kronos ("Session Browser Polish v2" -- "Sorting: Newly Created"):
    // real wall-clock seconds the host's own NetworkSession actually
    // started at (NetworkSession::sessionStartUnixSeconds()) -- not a
    // fabricated/estimated value, and not the same thing as
    // `lastSeenSeconds` on the browser side (that one is "how long since
    // I last heard an Announce," this one is "when the session began").
    int64_t sessionStartUnixSeconds = 0;
};

void serializeLanAnnouncement(const LanSessionAnnouncement& announcement, ByteWriter& writer);
// Real, bounds-checked -- returns false (leaving outAnnouncement
// untouched) on any read error, the same contract every other
// deserializeX() in net/Serialization.hpp already carries.
[[nodiscard]] bool deserializeLanAnnouncement(ByteReader& reader, LanSessionAnnouncement& outAnnouncement);

} // namespace engine::net
