#include "net/RollbackNetSession.hpp"

#include <algorithm>
#include <cstring>

namespace engine::net {

namespace {

constexpr uint8_t kLobbyType = 0xB8;
constexpr uint16_t kRollbackProtocolVersion = 1;
constexpr uint8_t kLobbyChannel = 0;
constexpr uint8_t kInputChannel = 1;

enum LobbyMessage : uint8_t { Hello = 'H', Welcome = 'W', Start = 'S', Reject = 'R' };

std::vector<uint8_t> message(LobbyMessage kind) { return {kLobbyType, kind}; }

template <typename T>
void put(std::vector<uint8_t>& out, const T& value) {
    const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
    out.insert(out.end(), bytes, bytes + sizeof(T));
}

template <typename T>
bool get(const uint8_t* data, size_t size, size_t at, T& out) {
    if (at + sizeof(T) > size) return false;
    std::memcpy(&out, data + at, sizeof(T));
    return true;
}

} // namespace

RollbackNetSession::~RollbackNetSession() { shutdown(); }

bool RollbackNetSession::start(const Config& config, StartFn onStart, std::string* error) {
    shutdown();
    config_ = config;
    onStart_ = std::move(onStart);
    transport_ = std::make_unique<ENetTransport>();
    transport_->setSimulatedLatencyMs(config.simulatedLatencyMs);
    transport_->setSimulatedPacketLossPercent(config.simulatedLossPercent);
    if (config.host) {
        players_ = std::clamp<uint32_t>(config.players, 1, kMaxRollbackPlayers);
        if (!transport_->hostServer(config.port, players_ > 1 ? players_ - 1 : 1)) {
            if (error) *error = "could not host on port " + std::to_string(config.port);
            transport_.reset();
            return false;
        }
        localPlayer_ = 0;
        phase_ = Phase::Lobby;
        maybeStartAsHost();
    } else {
        if (!transport_->connectToServer(config.address, config.port)) {
            if (error) *error = "could not connect to " + config.address + ":" + std::to_string(config.port);
            transport_.reset();
            return false;
        }
        phase_ = Phase::Lobby;
    }
    return true;
}

void RollbackNetSession::shutdown() {
    if (transport_) {
        transport_->flush();
        transport_->shutdown();
    }
    transport_.reset();
    peer_.reset();
    rollback_ = nullptr;
    phase_ = Phase::Idle;
    players_ = 0;
    localPlayer_.reset();
    playerByPeer_.clear();
    connectedToHost_ = false;
}

uint32_t RollbackNetSession::connectedPlayers() const {
    if (config_.host) return 1 + static_cast<uint32_t>(playerByPeer_.size());
    return connectedToHost_ ? 2 : 1;
}

float RollbackNetSession::roundTripMs() const {
    if (!transport_) return 0.0f;
    if (!config_.host) return transport_->roundTripTimeMs(0);
    float worst = 0.0f;
    for (const auto& [peer, player] : playerByPeer_) worst = std::max(worst, transport_->roundTripTimeMs(peer));
    return worst;
}

void RollbackNetSession::sendLobby(ENetTransport::PeerId to, const std::vector<uint8_t>& bytes) {
    transport_->send(to, bytes.data(), bytes.size(), kLobbyChannel, true);
}

void RollbackNetSession::poll() {
    if (!transport_) return;
    ENetTransport::Callbacks callbacks;
    callbacks.onPeerConnected = [this](ENetTransport::PeerId peer) {
        if (config_.host) return;
        connectedToHost_ = true;
        std::vector<uint8_t> hello = message(Hello);
        put(hello, kRollbackProtocolVersion);
        sendLobby(peer, hello);
    };
    callbacks.onPeerDisconnected = [this](ENetTransport::PeerId peer) {
        if (!config_.host) {
            connectedToHost_ = false;
            end(phase_ == Phase::Lobby && !localPlayer_ ? "could not reach the host" : "the host left the match");
            return;
        }
        auto it = playerByPeer_.find(peer);
        if (it == playerByPeer_.end()) return;
        playerByPeer_.erase(it);
        if (phase_ == Phase::Running) end("a player left the match");
    };
    callbacks.onPacketReceived = [this](ENetTransport::PeerId from, const uint8_t* data, size_t size, uint8_t channel) {
        onPacket(from, data, size, channel);
    };
    transport_->poll(0, callbacks);
}

void RollbackNetSession::onPacket(ENetTransport::PeerId from, const uint8_t* data, size_t size, uint8_t channel) {
    if (channel == kInputChannel) {
        if (phase_ != Phase::Running || !peer_) return;
        if (config_.host) {
            auto it = playerByPeer_.find(from);
            RollbackPacket packet;
            if (it == playerByPeer_.end() || !decodeRollbackPacket(data, size, packet) || packet.player != it->second) {
                return;
            }
            for (const auto& [peer, player] : playerByPeer_) {
                if (peer != from) transport_->send(peer, data, size, kInputChannel, false);
            }
        }
        peer_->receive(data, size);
        return;
    }
    if (size < 2 || data[0] != kLobbyType) return;
    const auto kind = static_cast<LobbyMessage>(data[1]);
    if (config_.host) {
        if (kind != Hello) return;
        uint16_t version = 0;
        if (!get(data, size, 2, version) || version != kRollbackProtocolVersion) {
            std::vector<uint8_t> reject = message(Reject);
            const std::string reason = "the host runs a different version of the game";
            reject.insert(reject.end(), reason.begin(), reason.end());
            sendLobby(from, reject);
            transport_->disconnectPeerGracefully(from);
            return;
        }
        if (phase_ != Phase::Lobby || playerByPeer_.count(from) || playerByPeer_.size() + 1 >= players_) {
            std::vector<uint8_t> reject = message(Reject);
            const std::string reason = phase_ == Phase::Lobby ? "the match is full" : "the match already started";
            reject.insert(reject.end(), reason.begin(), reason.end());
            sendLobby(from, reject);
            transport_->disconnectPeerGracefully(from);
            return;
        }
        auto taken = [&](uint32_t index) {
            for (const auto& [peer, player] : playerByPeer_) {
                if (player == index) return true;
            }
            return false;
        };
        uint32_t index = 1;
        while (taken(index)) ++index;
        playerByPeer_[from] = index;
        std::vector<uint8_t> welcome = message(Welcome);
        put(welcome, static_cast<uint8_t>(index));
        put(welcome, static_cast<uint8_t>(players_));
        sendLobby(from, welcome);
        maybeStartAsHost();
        return;
    }
    switch (kind) {
        case Welcome: {
            uint8_t index = 0;
            uint8_t players = 0;
            if (!get(data, size, 2, index) || !get(data, size, 3, players)) return;
            localPlayer_ = index;
            players_ = players;
            break;
        }
        case Start: {
            uint8_t players = 0;
            uint64_t checksum = 0;
            if (phase_ != Phase::Lobby || !localPlayer_ || !get(data, size, 2, players) ||
                !get(data, size, 3, checksum)) {
                return;
            }
            begin(players, *localPlayer_, checksum);
            break;
        }
        case Reject:
            end(std::string(reinterpret_cast<const char*>(data) + 2, size - 2));
            break;
        default:
            break;
    }
}

void RollbackNetSession::maybeStartAsHost() {
    if (!config_.host || phase_ != Phase::Lobby || playerByPeer_.size() + 1 < players_) return;
    begin(players_, 0, std::nullopt);
    if (phase_ != Phase::Running) return;
    std::vector<uint8_t> start = message(Start);
    put(start, static_cast<uint8_t>(players_));
    put(start, rollback_->initialChecksum());
    for (const auto& [peer, player] : playerByPeer_) sendLobby(peer, start);
    transport_->flush();
}

void RollbackNetSession::begin(uint32_t players, uint32_t localPlayer, std::optional<uint64_t> hostChecksum) {
    players_ = players;
    rollback_ = onStart_ ? onStart_(players, localPlayer) : nullptr;
    if (!rollback_ || rollback_->settings().players != players || rollback_->settings().localPlayer != localPlayer) {
        end("the game could not start the match");
        return;
    }
    if (hostChecksum && *hostChecksum != rollback_->initialChecksum()) {
        end("this copy of the game differs from the host's");
        return;
    }
    peer_ = std::make_unique<RollbackPeer>(*rollback_);
    phase_ = Phase::Running;
}

void RollbackNetSession::end(const std::string& reason) {
    if (phase_ == Phase::Ended) return;
    phase_ = Phase::Ended;
    endReason_ = reason;
}

void RollbackNetSession::tick(const core::RollbackInput& localInput) {
    poll();
    if (phase_ != Phase::Running || !peer_ || !rollback_) {
        if (transport_) transport_->flush();
        return;
    }
    peer_->tick(localInput);
    if (const auto frame = rollback_->desyncFrame()) {
        end("the players' games went out of sync at frame " + std::to_string(*frame));
    } else if (rollback_->restoreFailed()) {
        end("objects were created or removed during the match, which rollback can't undo");
    }
    const std::vector<uint8_t> packet = peer_->buildPacket();
    transport_->send(ENetTransport::kBroadcast, packet.data(), packet.size(), kInputChannel, false);
    transport_->flush();
}

} // namespace engine::net
