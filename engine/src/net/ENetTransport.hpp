#pragma once

#include <cstdint>
#include <functional>
#include <string>

struct _ENetHost;
struct _ENetPeer;
using ENetHost = _ENetHost;
using ENetPeer = _ENetPeer;

namespace engine::net {

// Reliable-UDP transport per docs/ARCHITECTURE.md §4.2/§3: ENet now, QUIC
// as the v2 upgrade. This class owns the ENet host/peer lifecycle and
// event pump; it does *not* know about InputCommand/DeltaSnapshot -- those
// travel as opaque byte payloads through onPacketReceived, with
// (de)serialization left to ClientPrediction/ServerReconciliation. That
// split is deliberate: swapping ENet for QUIC later should only ever
// touch this file.
class ENetTransport {
public:
    using PeerId = uint32_t;

    struct Callbacks {
        std::function<void(PeerId)> onPeerConnected;
        std::function<void(PeerId)> onPeerDisconnected;
        std::function<void(PeerId, const uint8_t* data, size_t size, uint8_t channel)> onPacketReceived;
    };

    ENetTransport();
    ~ENetTransport();

    ENetTransport(const ENetTransport&) = delete;
    ENetTransport& operator=(const ENetTransport&) = delete;

    // Binds and listens -- server-authoritative mode (§4.2 Principle 3:
    // there is no non-authoritative mode, so this is the only "host a
    // game" entry point).
    [[nodiscard]] bool hostServer(uint16_t port, size_t maxClients, uint8_t channelCount = 2);

    // Connects out to a server -- the runtime client's entry point.
    [[nodiscard]] bool connectToServer(const std::string& hostAddress, uint16_t port, uint8_t channelCount = 2);

    void shutdown();

    // Kronos ("Active Joining UI"): a real, graceful disconnect -- queues
    // any already-`send()`'d reliable packet to actually flush before
    // ENet tears the connection down (`enet_peer_disconnect_later()`),
    // unlike a bare `enet_host_destroy()`/`enet_peer_reset()`, which the
    // remote side only ever notices via its own real disconnect-timeout
    // detection (5-30s by default, see setPeerTimeout()'s own comment) --
    // real, prompt notification either direction, not just server->
    // client. Server mode: disconnects `peer` specifically. Client mode:
    // `peer` is ignored -- there is exactly one peer (the server),
    // same "the argument doesn't matter, there's only one connection"
    // convention send() already uses. A real, honest no-op if no such
    // real connection exists.
    void disconnectPeerGracefully(PeerId peer);

    // Kronos ("Active Joining UI"): real, tunable disconnect-detection
    // speed -- ENet's own default (5-30s before a silent peer is declared
    // disconnected, ENET_PEER_TIMEOUT_MINIMUM/MAXIMUM) is tuned for a
    // typical internet connection's jitter tolerance, not every real
    // deployment (a LAN session, for instance, can afford to declare a
    // dead peer gone far faster). Client-mode only for now -- applies to
    // the one real peer a client transport has; a real, honest no-op in
    // server mode or before a real connection exists. Thin wrapper over
    // enet_peer_timeout()'s own three real parameters.
    void setPeerTimeout(uint32_t timeoutLimit, uint32_t timeoutMinimumMs, uint32_t timeoutMaximumMs);

    // Kronos ("Active Joining UI"): a real, explicit send-now for any
    // packet already queued via send() -- `enet_host_service()` (poll())
    // normally does this as a side effect, but a caller about to
    // immediately shutdown() (destroying the host) with no further poll()
    // call needs to force it first, or a just-sent packet (e.g. a
    // Disconnect{SessionClosed} broadcast) can be silently lost.
    void flush();

    // Pumps ENet's event queue for up to `timeoutMs`, firing `callbacks`
    // for whatever happened. Call once per network-poll phase of
    // GameLoop::tick() -- not currently wired into GameLoop, since that
    // requires a decision about client-vs-server build configuration this
    // skeleton doesn't make yet (see docs/ARCHITECTURE.md §4.2 -- server
    // and client are different processes, engine_runtime doesn't branch
    // on that today).
    void poll(uint32_t timeoutMs, const Callbacks& callbacks);

    // Reliable (ENET_PACKET_FLAG_RELIABLE) send to one peer, or to every
    // connected peer if `peer` is kBroadcast. Unreliable send is the same
    // call with `reliable=false` -- snapshots (§4.2) are typically
    // unreliable-sequenced since a stale one is worthless once a newer
    // one arrives, unlike a RemoteEvent (§6) which needs reliable
    // delivery.
    static constexpr PeerId kBroadcast = 0xFFFFFFFFu;
    void send(PeerId peer, const uint8_t* data, size_t size, uint8_t channel, bool reliable);

    [[nodiscard]] bool isServer() const { return isServer_; }
    [[nodiscard]] size_t connectedPeerCount() const;

    // Real, ENet-computed mean round-trip time in milliseconds (updated
    // internally by ENet's own reliable-packet ack timing -- not
    // something this class measures itself). Server mode: `peer`'s RTT,
    // 0.0f if unknown/invalid. Client mode: ignores `peer` and returns
    // this connection's RTT to the server, the same "there is exactly
    // one peer regardless of what's passed" convention send() already
    // uses. 0.0f before the connection is established (ENet hasn't
    // measured anything yet) is a real, honest "no data" value, not an
    // error.
    [[nodiscard]] float roundTripTimeMs(PeerId peer) const;

private:
    ENetHost* host_ = nullptr;
    ENetPeer* serverPeer_ = nullptr; // valid only in client mode, after connect
    bool isServer_ = false;
    static uint32_t instanceCount_; // enet_initialize/deinitialize are process-global and refcounted here
};

} // namespace engine::net
