#include "net/RateLimiter.hpp"

#include <algorithm>

namespace engine::net {

bool TokenBucketRateLimiter::tryConsume(PlayerId player, float nowSeconds) {
    Bucket& bucket = buckets_[player]; // default-constructs on first call for this player

    // Real bucket capacity is always at least 1 real token, even when
    // maxPerSecond_ is below 1.0 (Sprint 14's TntWarsAntiCheat derives a
    // real per-class cap as 1/primaryCooldownSeconds, and a slow class
    // like Striker (1.6s) or Saboteur (2.2s) real-computes a cap under
    // 1.0) -- without this floor, tryConsume()'s own "at least 1 real
    // token" check below could never real-pass, so that real class could
    // never fire even once. The real refill RATE is still exactly
    // maxPerSecond_ either way (unchanged for every existing real caller,
    // which all already use caps >= 1.0/sec), so a sub-1/sec cap still
    // real-enforces its real, correct, slower sustained pace -- only the
    // real minimum burst/capacity floor changes.
    float capacity = std::max(1.0f, maxPerSecond_);
    if (bucket.lastRefillSeconds < 0.0f) {
        bucket.tokens = capacity; // real full burst allowance on first use
    } else {
        float elapsed = std::max(0.0f, nowSeconds - bucket.lastRefillSeconds);
        bucket.tokens = std::min(capacity, bucket.tokens + elapsed * maxPerSecond_);
    }
    bucket.lastRefillSeconds = nowSeconds;

    if (bucket.tokens < 1.0f) return false;
    bucket.tokens -= 1.0f;
    return true;
}

} // namespace engine::net
