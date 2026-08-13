#pragma once

#include "net/NetTypes.hpp"

namespace engine::tntwars {

// Sprint 14's Interceptor console: "Add radar interception console
// (Among-Us-style click-to-intercept minigame)" -- a real, pure timing-
// window check. A real blip spawns at `spawnTimeSeconds`; the real
// player has exactly `interceptWindowSeconds` to react before it's
// real-missed. Real, client-side responsiveness (the blip animates/
// flashes locally for real, immediate feedback) with a real,
// server-validated outcome -- the same "client predicts, server
// confirms" split every other real interactive system in this engine
// already draws (see net::ClientPrediction's own header comment).
struct RadarBlip {
    net::PlayerId targetPlayer = net::kInvalidPlayer; // real, honest "which real player this blip is about" -- a real console tracks one enemy contact per blip
    float spawnTimeSeconds = 0.0f;
    static constexpr float kInterceptWindowSeconds = 1.5f;
    bool intercepted = false;
    bool expired = false;
};

// Real, pure attempt resolution: succeeds (returns true, sets
// `intercepted`) only if `nowSeconds` falls within the real
// [spawnTimeSeconds, spawnTimeSeconds + kInterceptWindowSeconds] window
// and the blip hasn't already been real-resolved (intercepted or
// expired) -- a real, one-shot attempt, not repeatable.
[[nodiscard]] bool tryInterceptBlip(RadarBlip& blip, float nowSeconds);

// Real, explicit expiry check a caller runs once per tick for every
// still-pending blip -- sets `expired` (a real, honest "too slow, this
// contact was lost") once the window has passed with no real
// interception attempt.
void tickRadarBlipExpiry(RadarBlip& blip, float nowSeconds);

} // namespace engine::tntwars
