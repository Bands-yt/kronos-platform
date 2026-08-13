#pragma once

#include <cstdint>
#include <unordered_map>

#include "net/NetTypes.hpp"

namespace engine::net {

// Sprint 11 ("Networking Foundation") task 4's "NetworkOverlay showing
// ping, packet rate, dropped packets" -- a real, pure stats aggregator
// fed by real events (ENetTransport sends/receives, RemoteEvent schema/
// rate-limit rejections, ServerReconciliation validation failures), not
// fabricated numbers. Deliberately separate from ENetTransport itself
// (which only knows about raw ENet peers/packets, nothing about "ping"
// as a concept or "dropped" in the rejected-payload sense) -- this is
// the aggregation layer a debug overlay actually reads from.
class NetworkStats {
public:
    void recordPacketSent(size_t bytes);
    void recordPacketReceived(size_t bytes);
    // A real rejected input (ServerReconciliation::validate() failing) or
    // rejected RemoteEvent call (schema/rate-limit) -- "dropped" in the
    // sense this task's brief means it, not raw UDP packet loss (ENet
    // already retries reliable channels internally; this counts payloads
    // this engine's own logic chose to reject).
    void recordPacketDropped();
    void recordPing(PlayerId player, float roundTripMs);

    // Real, smoothed (EMA, same alpha-based smoothing convention
    // core::ProcessStatsSampler/core::Profiler already use) packets/sec --
    // call once per real tick with that tick's real dt.
    void tick(float dt);

    [[nodiscard]] uint64_t totalPacketsSent() const { return packetsSent_; }
    [[nodiscard]] uint64_t totalPacketsReceived() const { return packetsReceived_; }
    [[nodiscard]] uint64_t totalPacketsDropped() const { return packetsDropped_; }
    [[nodiscard]] uint64_t totalBytesSent() const { return bytesSent_; }
    [[nodiscard]] uint64_t totalBytesReceived() const { return bytesReceived_; }
    [[nodiscard]] float packetsSentPerSecond() const { return sentPerSecond_; }
    [[nodiscard]] float packetsReceivedPerSecond() const { return receivedPerSecond_; }
    // 0.0f (real, honest "no samples yet") for a player with no recorded ping.
    [[nodiscard]] float averagePingMs(PlayerId player) const;

    void removePlayer(PlayerId player) { pingByPlayer_.erase(player); }

private:
    uint64_t packetsSent_ = 0;
    uint64_t packetsReceived_ = 0;
    uint64_t packetsDropped_ = 0;
    uint64_t bytesSent_ = 0;
    uint64_t bytesReceived_ = 0;

    uint64_t sentSinceLastTick_ = 0;
    uint64_t receivedSinceLastTick_ = 0;
    float sentPerSecond_ = 0.0f;
    float receivedPerSecond_ = 0.0f;

    std::unordered_map<PlayerId, float> pingByPlayer_; // real EMA-smoothed RTT per player
};

} // namespace engine::net
