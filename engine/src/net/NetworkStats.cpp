#include "net/NetworkStats.hpp"

namespace engine::net {

void NetworkStats::recordPacketSent(size_t bytes) {
    ++packetsSent_;
    bytesSent_ += bytes;
    ++sentSinceLastTick_;
}

void NetworkStats::recordPacketReceived(size_t bytes) {
    ++packetsReceived_;
    bytesReceived_ += bytes;
    ++receivedSinceLastTick_;
}

void NetworkStats::recordPacketDropped() { ++packetsDropped_; }

void NetworkStats::recordPing(PlayerId player, float roundTripMs) {
    auto it = pingByPlayer_.find(player);
    if (it == pingByPlayer_.end()) {
        pingByPlayer_[player] = roundTripMs; // first sample -- start at the real measured value, no fake ramp-up
    } else {
        constexpr float kSmoothing = 0.2f;
        it->second = it->second * (1.0f - kSmoothing) + roundTripMs * kSmoothing;
    }
}

void NetworkStats::tick(float dt) {
    if (dt <= 0.0f) return;
    float instantSent = static_cast<float>(sentSinceLastTick_) / dt;
    float instantReceived = static_cast<float>(receivedSinceLastTick_) / dt;
    constexpr float kSmoothing = 0.15f;
    sentPerSecond_ = sentPerSecond_ * (1.0f - kSmoothing) + instantSent * kSmoothing;
    receivedPerSecond_ = receivedPerSecond_ * (1.0f - kSmoothing) + instantReceived * kSmoothing;
    sentSinceLastTick_ = 0;
    receivedSinceLastTick_ = 0;
}

float NetworkStats::averagePingMs(PlayerId player) const {
    auto it = pingByPlayer_.find(player);
    return it != pingByPlayer_.end() ? it->second : 0.0f;
}

} // namespace engine::net
