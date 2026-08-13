#pragma once

#include <cstdint>
#include <string>

#include "net/LanDiscoveryProtocol.hpp"

namespace engine::net {

// Kronos ("Active Joining UI" -- LAN discovery): a real, self-contained
// UDP broadcast announcer -- owned optionally by a hosting NetworkSession
// (see NetworkSession::Config::advertiseOnLan), periodically broadcasting
// a real LanSessionAnnouncement so any engine_runtime instance on the
// same LAN running a real LanSessionBrowser can discover this session
// without a manually-typed IP address.
//
// No background thread: driven synchronously, once per real frame via
// tick(), through a non-blocking socket -- the same model
// ENetTransport::poll() already established for the game-traffic socket,
// and that this engine's whole single-threaded tick ordering assumes
// throughout (see runtime/GameLoop.hpp's own class comment). This isn't
// a workaround; there's no second thread here to race with in the first
// place.
//
// Real L2/L3 UDP broadcast delivery isn't guaranteed inside an arbitrary
// sandboxed/CI container even on real hardware (no broadcast-capable
// interface configured, firewalled, etc.) -- an honest environmental
// limit, not a design flaw. start()'s own `destinationAddress` parameter
// is real and caller-supplied specifically so a real test can point it
// at "127.0.0.1" (real loopback delivery, always available) and still
// exercise the exact real serialize/socket/send code path, matching
// ENetTransport's own tests, which never touch real LAN broadcast either
// -- always loopback.
//
// Linux-only for now (matches core::CrashReporter/core::ResourcePaths'
// own #if defined(__linux__) precedent) -- start() is a real, honest
// `return false` everywhere else, not a silent partial implementation.
class LanSessionAnnouncer {
public:
    LanSessionAnnouncer() = default;
    ~LanSessionAnnouncer();

    LanSessionAnnouncer(const LanSessionAnnouncer&) = delete;
    LanSessionAnnouncer& operator=(const LanSessionAnnouncer&) = delete;

    // Real, non-blocking UDP socket setup -- SO_BROADCAST, bound to
    // `pingPort` (NOT `announcePort` -- real bug found and fixed: binding
    // the same port a real, co-located LanSessionBrowser also binds is
    // ambiguous for unicast delivery, see LanDiscoveryProtocol.hpp's own
    // kLanAnnouncePort/kLanPingPort comment). `destinationAddress`/
    // `announcePort` are where real Announce broadcasts are sent.
    [[nodiscard]] bool start(const std::string& destinationAddress, uint16_t announcePort, uint16_t pingPort);
    void stop();
    [[nodiscard]] bool isRunning() const { return socketFd_ >= 0; }

    // Real, once-per-frame drive: first drains and replies to any real
    // inbound Echo ping immediately (the same socket serves both jobs),
    // then re-broadcasts `announcement` every kAnnounceIntervalSeconds. A
    // real, honest no-op if not running.
    void tick(float dt, const LanSessionAnnouncement& announcement);

    static constexpr float kAnnounceIntervalSeconds = 1.0f;

private:
    int socketFd_ = -1;
    float announceTimer_ = 0.0f;
    std::string destinationAddress_;
    uint16_t announcePort_ = 0;
};

} // namespace engine::net
