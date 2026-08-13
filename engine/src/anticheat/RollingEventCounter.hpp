#pragma once

#include <cstddef>
#include <deque>
#include <unordered_map>

#include "anticheat/BehavioralTelemetry.hpp"

namespace engine::anticheat {

// Sprint 12 ("Moderation & Safety Systems") task 3's real, shared,
// rolling-window event counter -- the anti-cheat-foundation counterpart
// to net::TokenBucketRateLimiter (same "caller supplies nowSeconds
// explicitly, so it's pure and headlessly testable with fabricated
// timestamps" design). Deliberately generic rather than two near-
// identical classes: a real "too many server-rejected movement inputs
// recently" tracker and a real "too many core::Economy EarnThrottle cap
// hits recently" tracker are the exact same shape (count real events per
// player within a real trailing window), so this is the one, shared
// primitive both build on -- the same "one real call site, can't drift"
// reasoning behind sharing net::isMovementPlausible() between Sprint
// 11's server authority and this sprint's anti-cheat foundation.
//
// This is intentionally NOT the "behavioral ML model" BehavioralTelemetry's
// own header comment defers to a real future service (§3's "Rust
// service + ONNX Runtime anomaly model" stack entry) -- that boundary is
// deliberate and this class doesn't cross it. This is a much simpler,
// honestly-scoped rule ("N real events in T real seconds"), not a claim
// of behavioral modeling.
class RollingEventCounter {
public:
    explicit RollingEventCounter(float windowSeconds) : windowSeconds_(windowSeconds) {}

    void recordEvent(PlayerId player, float nowSeconds);

    // Real count of events for `player` within the trailing window ending
    // at `nowSeconds` -- also real-prunes that player's own stale
    // entries as a side effect (safe: the pruned entries are, by
    // definition, already outside anything a subsequent call at the same
    // or a later `nowSeconds` could still count).
    [[nodiscard]] size_t countInWindow(PlayerId player, float nowSeconds);

    [[nodiscard]] bool isSuspicious(PlayerId player, float nowSeconds, size_t threshold);

    void removePlayer(PlayerId player) { eventTimestamps_.erase(player); }

private:
    std::unordered_map<PlayerId, std::deque<float>> eventTimestamps_;
    float windowSeconds_;
};

} // namespace engine::anticheat
