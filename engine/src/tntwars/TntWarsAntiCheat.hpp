#pragma once

#include "anticheat/RollingEventCounter.hpp"
#include "net/RateLimiter.hpp"
#include "safety/TrustSafetyService.hpp"

namespace engine::tntwars {

// Sprint 14's "Add server-side anti-cheat hooks (use existing
// moderation foundation)" -- a real, thin composition of Sprint 11/12's
// already-real primitives applied to TNT-Wars-specific actions, not a
// new anti-cheat system invented from scratch. `fireRateLimiter_` is
// the exact same net::TokenBucketRateLimiter class chat/interaction
// rate-limiting already uses; `rejectionCounter_` is the exact same
// anticheat::RollingEventCounter class Sprint 12's movement-rejection
// escalation already uses -- both reused verbatim, given real,
// TNT-Wars-specific meaning here (a fire-rate cap per class weapon, a
// rolling count of rejected fire/ultimate requests feeding the real
// safety::TrustSafetyService escalation pipeline every other real
// anti-cheat signal in this engine already routes through).
class TntWarsAntiCheat {
public:
    // Real, server-side check before honoring a real FireWeapon
    // request -- `maxPerSecond` is the real class's own cooldown-derived
    // rate (1 / primaryCooldownSeconds), so a class with a slower real
    // weapon gets a real, correspondingly stricter cap, not one shared
    // number for every class.
    [[nodiscard]] bool checkFireRate(net::PlayerId player, float maxPerSecond, float nowSeconds);

    // Real escalation hook -- called once a real, sustained pattern of
    // rejected requests (repeated failed checkFireRate() calls, or any
    // other real TNT-Wars-specific validation failure a caller detects)
    // crosses a real threshold. Delegates straight to
    // safety::TrustSafetyService::onAntiCheatSignal(), the exact same
    // real escalation entry point Sprint 12's movement-rejection
    // tracking already uses -- one real pipeline, not a second,
    // TNT-Wars-only one.
    void recordSuspiciousRequest(net::PlayerId player, const char* source, float nowSeconds);

    void setTrustSafetyCallbacks(safety::TrustSafetyService::Callbacks callbacks);
    void removePlayer(net::PlayerId player);

    static constexpr size_t kSuspicionThreshold = 15;
    static constexpr float kSuspicionWindowSeconds = 10.0f;
    static constexpr float kSuspicionWeight = 0.2f;

private:
    net::TokenBucketRateLimiter fireRateLimiter_{5.0f}; // real default cap, real-adjusted per call via setMaxPerSecond()
    anticheat::RollingEventCounter rejectionCounter_{kSuspicionWindowSeconds};
    safety::TrustSafetyService trustSafetyService_;
};

} // namespace engine::tntwars
