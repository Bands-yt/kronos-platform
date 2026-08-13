#pragma once

#include <unordered_map>

#include "net/NetTypes.hpp"

namespace engine::net {

// Real, pure, per-player token-bucket rate limiter -- the same real
// algorithm net::RemoteEvent::checkRateLimit() already implements
// inline (kept there, private, for that class's own historical/low-risk
// reasons), extracted here as a real, independently reusable, and
// crucially *pure* (caller supplies `nowSeconds` rather than this class
// reading `std::chrono::steady_clock::now()` itself, so it's fully
// headlessly testable with fabricated timestamps) primitive for every
// other rate-limited real-time action this codebase needs: Sprint 11's
// interaction-rate validation and Sprint 12's chat/mining-rate anti-
// cheat checks both build on this one class rather than each
// reimplementing the same bucket math a third and fourth time.
class TokenBucketRateLimiter {
public:
    explicit TokenBucketRateLimiter(float maxPerSecond) : maxPerSecond_(maxPerSecond) {}

    // Real token-bucket check: refills `player`'s bucket based on real
    // elapsed time since its last refill, then consumes one token if
    // available. A player's first-ever call starts with a full burst
    // allowance (the same "don't punish the very first action" real
    // convention net::RemoteEvent's own limiter already established).
    [[nodiscard]] bool tryConsume(PlayerId player, float nowSeconds);

    void reset(PlayerId player) { buckets_.erase(player); }
    void setMaxPerSecond(float maxPerSecond) { maxPerSecond_ = maxPerSecond; }
    [[nodiscard]] float maxPerSecond() const { return maxPerSecond_; }

private:
    struct Bucket {
        float tokens = 0.0f;
        float lastRefillSeconds = -1.0f; // negative = "never refilled yet", real sentinel for the first-call case
    };

    float maxPerSecond_;
    std::unordered_map<PlayerId, Bucket> buckets_;
};

} // namespace engine::net
