#include "tntwars/TntWarsAntiCheat.hpp"

namespace engine::tntwars {

bool TntWarsAntiCheat::checkFireRate(net::PlayerId player, float maxPerSecond, float nowSeconds) {
    fireRateLimiter_.setMaxPerSecond(maxPerSecond);
    return fireRateLimiter_.tryConsume(player, nowSeconds);
}

void TntWarsAntiCheat::recordSuspiciousRequest(net::PlayerId player, const char* source, float nowSeconds) {
    rejectionCounter_.recordEvent(player, nowSeconds);
    if (rejectionCounter_.isSuspicious(player, nowSeconds, kSuspicionThreshold)) {
        trustSafetyService_.onAntiCheatSignal(player, source, kSuspicionWeight);
    }
}

void TntWarsAntiCheat::setTrustSafetyCallbacks(safety::TrustSafetyService::Callbacks callbacks) {
    trustSafetyService_.setCallbacks(std::move(callbacks));
}

void TntWarsAntiCheat::removePlayer(net::PlayerId player) { rejectionCounter_.removePlayer(player); }

} // namespace engine::tntwars
