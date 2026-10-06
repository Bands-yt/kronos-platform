#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

#include "net/ENetTransport.hpp"
#include "net/RollbackProtocol.hpp"

namespace engine::net {

// A peer-to-peer rollback match over ENet. One player hosts and relays
// every packet to the others (so only the host needs a reachable port);
// everyone simulates the whole game. The match starts once `players`
// players are connected; each side then builds its world and checks its
// starting state matches the host's, so different scene versions can't
// silently desync.
class RollbackNetSession {
public:
    enum class Phase { Idle, Lobby, Running, Ended };

    struct Config {
        bool host = true;
        std::string address = "127.0.0.1"; // join only
        uint16_t port = 7790;
        uint32_t players = 2;              // host only; joiners learn it from the host
        uint32_t simulatedLatencyMs = 0;   // testing aids
        uint8_t simulatedLossPercent = 0;
    };

    // Called once when the match starts: build the world for `players`
    // and return the PhysicsRollback driving it. Its initial checksum is
    // compared with the host's.
    using StartFn = std::function<core::PhysicsRollback*(uint32_t players, uint32_t localPlayer)>;

    RollbackNetSession() = default;
    ~RollbackNetSession();
    RollbackNetSession(const RollbackNetSession&) = delete;
    RollbackNetSession& operator=(const RollbackNetSession&) = delete;

    bool start(const Config& config, StartFn onStart, std::string* error = nullptr);
    void shutdown();

    // Call once per simulation frame. Handles the lobby, and while
    // running advances one frame with `localInput` (unless the session
    // decides to wait for slower peers) and exchanges packets.
    void tick(const core::RollbackInput& localInput);

    [[nodiscard]] Phase phase() const { return phase_; }
    [[nodiscard]] bool isHost() const { return config_.host; }
    [[nodiscard]] uint32_t players() const { return players_; }
    [[nodiscard]] uint32_t connectedPlayers() const;
    [[nodiscard]] std::optional<uint32_t> localPlayer() const { return localPlayer_; }
    // Why the match ended (a player left, a desync, a different scene).
    [[nodiscard]] const std::string& endReason() const { return endReason_; }
    [[nodiscard]] RollbackPeer* peer() { return peer_.get(); }
    [[nodiscard]] float roundTripMs() const;

private:
    void poll();
    void onPacket(ENetTransport::PeerId from, const uint8_t* data, size_t size, uint8_t channel);
    void begin(uint32_t players, uint32_t localPlayer, std::optional<uint64_t> hostChecksum);
    void end(const std::string& reason);
    void sendLobby(ENetTransport::PeerId to, const std::vector<uint8_t>& bytes);
    void maybeStartAsHost();

    Config config_;
    StartFn onStart_;
    std::unique_ptr<ENetTransport> transport_;
    std::unique_ptr<RollbackPeer> peer_;
    core::PhysicsRollback* rollback_ = nullptr;
    Phase phase_ = Phase::Idle;
    uint32_t players_ = 0;
    std::optional<uint32_t> localPlayer_;
    std::unordered_map<ENetTransport::PeerId, uint32_t> playerByPeer_; // host: joined clients
    bool connectedToHost_ = false;
    std::string endReason_;
};

} // namespace engine::net
