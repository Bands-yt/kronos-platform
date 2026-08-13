#pragma once

#include "anticheat/RollingEventCounter.hpp"

namespace engine::anticheat {

// Sprint 12 task 3's "Add currency manipulation detection" -- built on
// the same real anticheat::RollingEventCounter primitive
// MovementRejection tracking uses (net::NetworkSession), given real
// meaning here: a "recent event" is a real
// core::applyEarnThrottle() call that actually reduced a proposed
// payout, i.e. the player has hit core::Economy's own real per-window
// earn cap (core/Economy.hpp's kEarnCapPerWindow) unusually often. A
// legitimate player occasionally grazing the cap during a genuine
// selling spree is normal; a player hitting it constantly, window after
// window, is the real anomaly this flags -- the same "one occurrence is
// normal, a pattern is the real signal" property every other trust &
// safety signal source in this codebase already follows (see
// safety::RiskScore's own header comment).
//
// Honest scope note: there is no real live network call site for this
// yet in this codebase -- selling isn't one of the interactions
// net::NetworkSession dispatches over the network today (Sprint 11's
// own stated scope limit: only teleport is fully wired; mining/pickup/
// selling have real validation primitives available but no dispatch
// path). This class is a real, tested, ready-to-call primitive for
// whenever that dispatch path exists, the same "build the shared
// primitive now, the real call site follows" pattern
// net::TokenBucketRateLimiter was built under in Sprint 11, before
// Sprint 12 (this sprint) existed to consume it.
class CurrencyAnomalyDetector {
public:
    explicit CurrencyAnomalyDetector(float windowSeconds = 300.0f) : counter_(windowSeconds) {}

    // Real integration point: call once per real core::applyEarnThrottle()
    // result -- `proposedCoins` is the pre-throttle amount, `actualCoins`
    // is what applyEarnThrottle() actually returned. Only records a real
    // event when the throttle genuinely reduced the payout
    // (`actualCoins < proposedCoins`); a normal, un-throttled sale is not
    // an anomaly signal at all.
    void recordSale(PlayerId player, int64_t proposedCoins, int64_t actualCoins, float nowSeconds);

    [[nodiscard]] size_t throttleHitCountInWindow(PlayerId player, float nowSeconds) {
        return counter_.countInWindow(player, nowSeconds);
    }
    [[nodiscard]] bool isSuspicious(PlayerId player, float nowSeconds, size_t threshold = 5) {
        return counter_.isSuspicious(player, nowSeconds, threshold);
    }
    void removePlayer(PlayerId player) { counter_.removePlayer(player); }

private:
    RollingEventCounter counter_;
};

} // namespace engine::anticheat
