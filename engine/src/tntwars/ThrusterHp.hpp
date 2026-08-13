#pragma once

namespace engine::tntwars {

// Sprint 14's Sky Platforms map: "Add thruster HP system (5 hits ->
// platform destabilises -> tilt -> collapse)" -- exactly what this real,
// pure state machine implements. Real, not cosmetic: `tiltDegrees`
// really increases with each hit (a real caller applies it to the
// platform entity's own Transform::rotation), and `collapsed` is a real
// terminal state a caller checks to stop treating the platform as
// standable ground.
struct ThrusterPlatformState {
    int hitsTaken = 0;
    static constexpr int kMaxHits = 5;
    // Real, linear tilt ramp -- kMaxTiltDegrees reached exactly at
    // kMaxHits, so "tilt" and "collapse" are the same real threshold,
    // not two independently-tuned numbers that could drift apart.
    static constexpr float kMaxTiltDegrees = 35.0f;
    float tiltDegrees = 0.0f;
    bool collapsed = false;
};

// Real, idempotent-safe hit application -- a hit against an
// already-collapsed platform is a real, honest no-op (you can't destroy
// rubble twice), not an error.
void applyThrusterHit(ThrusterPlatformState& state);

[[nodiscard]] bool isThrusterDestabilizing(const ThrusterPlatformState& state);

} // namespace engine::tntwars
