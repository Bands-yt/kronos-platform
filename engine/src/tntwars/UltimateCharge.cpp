#include "tntwars/UltimateCharge.hpp"

#include <algorithm>

namespace engine::tntwars {

void UltimateChargeTracker::addCharge(net::PlayerId player, float amount, float maxCharge) {
    float& value = charge_[player];
    value = std::clamp(value + amount, 0.0f, maxCharge);
}

float UltimateChargeTracker::charge(net::PlayerId player) const {
    auto it = charge_.find(player);
    return it != charge_.end() ? it->second : 0.0f;
}

bool UltimateChargeTracker::isReady(net::PlayerId player, float required) const { return charge(player) >= required; }

void UltimateChargeTracker::consume(net::PlayerId player) { charge_[player] = 0.0f; }

void UltimateChargeTracker::removePlayer(net::PlayerId player) { charge_.erase(player); }

} // namespace engine::tntwars
