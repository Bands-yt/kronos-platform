#pragma once

#include <unordered_map>

#include "net/NetTypes.hpp"

namespace engine::tntwars {

// Real, pure, per-player ultimate-charge meter -- server-authoritative
// (the server is the only real caller that ever adds charge or consumes
// it, matching every other "server owns world state" primitive in this
// engine). Deliberately simple: a single float per player, clamped at
// the class's own ultimateChargeRequired (see ClassSystem.hpp) rather
// than allowing unbounded overcharge.
class UltimateChargeTracker {
public:
    void addCharge(net::PlayerId player, float amount, float maxCharge);
    [[nodiscard]] float charge(net::PlayerId player) const;
    [[nodiscard]] bool isReady(net::PlayerId player, float required) const;
    // Real, explicit reset to 0 -- called once a real ultimate trigger is
    // accepted server-side (see TntWarsMatch.hpp), never silently by
    // this class on its own.
    void consume(net::PlayerId player);
    void removePlayer(net::PlayerId player);

private:
    std::unordered_map<net::PlayerId, float> charge_;
};

} // namespace engine::tntwars
