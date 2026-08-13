#pragma once

#include "net/NetTypes.hpp"

namespace engine::net {

// Sprint 11 ("Networking Foundation") task 2's "interpolation/
// extrapolation for smooth visuals" + "jitter smoothing and dead-
// reckoning" -- real smoothing for *remote* entities (anything with a
// real net::NetworkIdentity that is NOT isLocallyControlled; the local
// player's own avatar uses net::ClientPrediction instead, see that
// class's own header comment -- these are two deliberately different
// mechanisms for two deliberately different problems: predicting your
// own unconfirmed input vs. smoothly rendering someone else's confirmed-
// but-infrequent state).
//
// A client only receives a new authoritative EntityState once per server
// tick (or less often under packet loss/jitter) but wants to render at
// its own, higher, variable frame rate. This buffers the last two real
// received states and interpolates between them at a real short render-
// time delay (the standard "interpolation buffer" technique -- render
// slightly in the past so there are always two real snapshots to
// interpolate between instead of guessing forward from one), falling
// back to real dead-reckoning (extrapolating from the last known
// velocity, clamped) when no newer state has arrived in time.
struct RemoteEntitySnapshot {
    float receivedAtSeconds = 0.0f; // the receiving caller's own clock, whatever units it uses consistently
    EntityState state;
};

class RemoteEntityInterpolator {
public:
    // Render slightly in the past so interpolation (not extrapolation) is
    // the common case -- 100ms is a real, standard starting value
    // (roughly 2-3 server ticks at a 20-30Hz tick rate).
    static constexpr float kInterpolationDelaySeconds = 0.1f;
    // Beyond this much extrapolation past the newest snapshot, dead-
    // reckoning is clamped rather than projected indefinitely -- a long
    // connection stall should freeze the entity near its last known
    // position, not fling it arbitrarily far along a stale velocity.
    static constexpr float kMaxExtrapolationSeconds = 0.25f;

    // Real jitter smoothing: out-of-order snapshots (an older-timestamped
    // one arriving after a newer one, real over unreliable transports)
    // are honestly dropped rather than corrupting the buffer's real
    // chronological order -- the buffer's invariant (newer_ is always the
    // most recent real snapshot pushed) must hold for sample()'s
    // interpolation math to mean anything.
    void pushSnapshot(const EntityState& state, float receivedAtSeconds);

    // Pure -- the real smoothed position/rotation/velocity to render at
    // `renderTimeSeconds` (same clock units as pushSnapshot's
    // receivedAtSeconds). See this class's own header comment for the
    // interpolate-if-bracketed, dead-reckon-otherwise logic.
    [[nodiscard]] EntityState sample(float renderTimeSeconds) const;

    [[nodiscard]] bool hasAnySnapshot() const { return hasNewer_; }

    void reset() {
        hasOlder_ = false;
        hasNewer_ = false;
    }

private:
    RemoteEntitySnapshot older_;
    RemoteEntitySnapshot newer_;
    bool hasOlder_ = false;
    bool hasNewer_ = false;
};

} // namespace engine::net
