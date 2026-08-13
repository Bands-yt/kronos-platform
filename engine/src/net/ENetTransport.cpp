#include "net/ENetTransport.hpp"

#include <cstdio>

#include <enet/enet.h>

namespace engine::net {

uint32_t ENetTransport::instanceCount_ = 0;

namespace {
ENetTransport::PeerId peerToId(ENetHost* host, ENetPeer* peer) {
    // ENet allocates every peer slot as one contiguous array (host->peers);
    // its index is a stable, cheap-to-compute id without needing our own
    // connection table. +1 so id 0 stays free for "no peer" sentinels.
    return static_cast<ENetTransport::PeerId>(peer - host->peers) + 1;
}
ENetPeer* idToPeer(ENetHost* host, ENetTransport::PeerId id) {
    return &host->peers[id - 1];
}
} // namespace

ENetTransport::ENetTransport() {
    if (instanceCount_ == 0 && enet_initialize() != 0) {
        std::fprintf(stderr, "ENetTransport: enet_initialize() failed.\n");
    }
    ++instanceCount_;
}

ENetTransport::~ENetTransport() {
    shutdown();
    if (--instanceCount_ == 0) {
        enet_deinitialize();
    }
}

bool ENetTransport::hostServer(uint16_t port, size_t maxClients, uint8_t channelCount) {
    ENetAddress address{};
    address.host = ENET_HOST_ANY;
    address.port = port;

    host_ = enet_host_create(&address, maxClients, channelCount, /*incomingBandwidth=*/0, /*outgoingBandwidth=*/0);
    if (!host_) {
        std::fprintf(stderr, "ENetTransport: enet_host_create (server) failed on port %u.\n", port);
        return false;
    }
    isServer_ = true;
    std::fprintf(stdout, "ENetTransport: listening on port %u (max %zu clients)\n", port, maxClients);
    return true;
}

bool ENetTransport::connectToServer(const std::string& hostAddress, uint16_t port, uint8_t channelCount) {
    host_ = enet_host_create(nullptr, /*peerCount=*/1, channelCount, 0, 0);
    if (!host_) {
        std::fprintf(stderr, "ENetTransport: enet_host_create (client) failed.\n");
        return false;
    }

    ENetAddress address{};
    enet_address_set_host(&address, hostAddress.c_str());
    address.port = port;

    serverPeer_ = enet_host_connect(host_, &address, channelCount, 0);
    if (!serverPeer_) {
        std::fprintf(stderr, "ENetTransport: enet_host_connect failed.\n");
        return false;
    }

    isServer_ = false;
    std::fprintf(stdout, "ENetTransport: connecting to %s:%u ...\n", hostAddress.c_str(), port);
    // NOTE: connection isn't established yet -- poll() must observe an
    // ENET_EVENT_TYPE_CONNECT (surfaced via Callbacks::onPeerConnected)
    // before send() will actually deliver anything.
    return true;
}

void ENetTransport::shutdown() {
    if (host_) {
        enet_host_destroy(host_);
        host_ = nullptr;
    }
    serverPeer_ = nullptr;
}

void ENetTransport::disconnectPeerGracefully(PeerId peer) {
    if (!host_) return;
    if (!isServer_) {
        // Client: there is exactly one peer (the server) regardless of
        // what the caller passed -- same convention send() already uses.
        if (serverPeer_) enet_peer_disconnect_later(serverPeer_, 0);
        return;
    }
    if (peer == kBroadcast || peer == 0 || peer > host_->peerCount) return;
    enet_peer_disconnect_later(idToPeer(host_, peer), 0);
}

void ENetTransport::setPeerTimeout(uint32_t timeoutLimit, uint32_t timeoutMinimumMs, uint32_t timeoutMaximumMs) {
    if (isServer_ || !serverPeer_) return;
    enet_peer_timeout(serverPeer_, timeoutLimit, timeoutMinimumMs, timeoutMaximumMs);
}

void ENetTransport::flush() {
    if (host_) enet_host_flush(host_);
}

void ENetTransport::poll(uint32_t timeoutMs, const Callbacks& callbacks) {
    if (!host_) return;

    ENetEvent event;
    while (enet_host_service(host_, &event, timeoutMs) > 0) {
        switch (event.type) {
            case ENET_EVENT_TYPE_CONNECT:
                if (callbacks.onPeerConnected) {
                    callbacks.onPeerConnected(peerToId(host_, event.peer));
                }
                break;
            case ENET_EVENT_TYPE_DISCONNECT:
                if (callbacks.onPeerDisconnected) {
                    callbacks.onPeerDisconnected(peerToId(host_, event.peer));
                }
                break;
            case ENET_EVENT_TYPE_RECEIVE:
                if (callbacks.onPacketReceived) {
                    callbacks.onPacketReceived(peerToId(host_, event.peer), event.packet->data,
                                                event.packet->dataLength, event.channelID);
                }
                enet_packet_destroy(event.packet);
                break;
            default:
                break;
        }
        // enet_host_service only blocks for `timeoutMs` on the *first*
        // call in this loop; once an event is available it returns
        // immediately, so this drains the whole queue in one poll() call
        // rather than one event per call.
        timeoutMs = 0;
    }
}

void ENetTransport::send(PeerId peer, const uint8_t* data, size_t size, uint8_t channel, bool reliable) {
    if (!host_) return;

    ENetPacket* packet = enet_packet_create(data, size, reliable ? ENET_PACKET_FLAG_RELIABLE : 0);

    if (!isServer_) {
        // Client: there is exactly one peer (the server) regardless of
        // what the caller passed.
        if (serverPeer_) enet_peer_send(serverPeer_, channel, packet);
        else enet_packet_destroy(packet);
        return;
    }

    if (peer == kBroadcast) {
        enet_host_broadcast(host_, channel, packet);
    } else {
        enet_peer_send(idToPeer(host_, peer), channel, packet);
    }
}

float ENetTransport::roundTripTimeMs(PeerId peer) const {
    if (!host_) return 0.0f;
    if (!isServer_) {
        return serverPeer_ ? static_cast<float>(serverPeer_->roundTripTime) : 0.0f;
    }
    if (peer == kBroadcast || peer == 0 || peer > host_->peerCount) return 0.0f;
    return static_cast<float>(idToPeer(host_, peer)->roundTripTime);
}

size_t ENetTransport::connectedPeerCount() const {
    if (!host_) return 0;
    size_t count = 0;
    for (size_t i = 0; i < host_->peerCount; ++i) {
        if (host_->peers[i].state == ENET_PEER_STATE_CONNECTED) ++count;
    }
    return count;
}

} // namespace engine::net
