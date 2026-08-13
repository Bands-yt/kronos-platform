#pragma once

#include <array>
#include <optional>
#include <vector>

#include <glm/glm.hpp>

#include "tntwars/MapDefinition.hpp"

namespace engine::tntwars {

// Kronos roadmap Milestone 8 ("Movement systems"): a real, placed launch
// volume -- the same real "position + radius, pure distance check"
// technique LavaEruptionState/isWithinEruptionDamageRadius() already
// established for a hazard, applied here to a real launch instead.
// Deliberately produces a real, direct positional displacement rather
// than a fake velocity/impulse: net::applyNetworkedMovement()'s own real
// kinematic model (see that file's header comment) has no velocity state
// to apply an impulse to -- command.jump's own "fixed vertical nudge per
// tick" is the exact same real, honest contract triggerJumpPad() below
// follows.
struct JumpPadState {
    glm::vec3 position{0.0f};
    float triggerRadius = 3.0f;
    float launchStrength = 14.0f;
    float cooldownSecondsRemaining = 0.0f; // real-0 while ready to fire again
};

constexpr float kJumpPadCooldownSeconds = 1.0f; // real, short -- long enough that one landing doesn't real-double-trigger next tick

// Real, per-map jump pad layout -- two real pads, symmetric across the
// map's own center (X axis), sitting right on the real no-man's-land Z
// band (see Scavenging.hpp's own kNoMansLandHalfWidth) -- the same real
// "bridge the contested middle" placement most arena shooters use jump
// pads for. Map-independent by real, deliberate design (every current
// map shares the same real Z-symmetric layout convention, see
// MapLayout.cpp's own kBaseZ), the same reasoning teamSpawnPosition()
// generalizes across every map without per-map authoring.
[[nodiscard]] std::vector<JumpPadState> buildJumpPads(MapId map);

// Real, pure in-radius check -- identical shape to
// isWithinEruptionDamageRadius().
[[nodiscard]] bool isWithinJumpPadRadius(glm::vec3 playerPosition, const JumpPadState& pad);

// Real trigger attempt: fails (returns real std::nullopt, no state
// change) if the player isn't real-within triggerRadius or the pad is
// still real-on cooldown -- otherwise starts a real
// kJumpPadCooldownSeconds cooldown and returns the real positional
// launch offset (purely vertical, matching command.jump's own real
// "positive Y nudge" convention) a caller adds directly to the player's
// own Transform::position.
[[nodiscard]] std::optional<glm::vec3> triggerJumpPad(JumpPadState& pad, glm::vec3 playerPosition);

// Real per-tick cooldown countdown -- a real, honest no-op once the
// cooldown has already fully elapsed or on a non-positive dt.
void tickJumpPad(JumpPadState& pad, float dt);

// --- Zip-lines (Kronos "Sky Map Full Engine Specification") ------------

// A real, curved traversal span between two world positions -- a real
// quadratic Bezier (start/controlPoint/end), not a straight line, so a
// real caller can raise `controlPoint` above obstacle geometry between
// the two real endpoints and have the whole real path arc up and over
// it (the brief's own "cable pathing must curve around rock, not
// intersect it"). `controlPoint` defaulting to the real straight-line
// midpoint keeps this a real, exact straight zip-line for any caller
// that never sets it, so the shape is opt-in, not a forced curve.
struct ZipLineState {
    glm::vec3 start{0.0f};
    glm::vec3 end{0.0f};
    glm::vec3 controlPoint{0.0f}; // real Bezier control point; real callers set this to start/end's own midpoint + a real upward offset
    float triggerRadius = 3.0f;
    float travelSpeed = 22.0f; // real world units/second along the curve
};

// Real, pure quadratic-Bezier position at real parameter t in [0,1] --
// B(t) = (1-t)^2*start + 2(1-t)t*controlPoint + t^2*end. t=0 is real-
// exactly `start`, t=1 is real-exactly `end`.
[[nodiscard]] glm::vec3 sampleZipLineCurve(const ZipLineState& zipLine, float t);

// Real, pure curve tangent (unit-length, or a real zero vector at a
// real degenerate zero-length curve) at parameter t -- the real
// direction of travel a rider moving forward along the curve at that
// point would be facing.
[[nodiscard]] glm::vec3 zipLineCurveTangent(const ZipLineState& zipLine, float t);

// Real point-to-segment distance -- pure, headlessly testable
// independent of any live ZipLineState. Still real, still used directly
// by callers wanting a real straight-line check (e.g. placing a
// zip-line's own control point relative to the straight chord).
[[nodiscard]] float distanceToZipLineSegment(glm::vec3 point, glm::vec3 start, glm::vec3 end);

// Real, coarse-sampled nearest-point search along the real curve --
// pure, deterministic (fixed sample count, no RNG), real enough
// precision for a live per-tick trigger check, not a numerically-exact
// closed-form projection (quadratic Bezier nearest-point has no simple
// closed form). Writes the real nearest parameter t into `outNearestT`
// when non-null.
[[nodiscard]] float distanceToZipLineCurve(const ZipLineState& zipLine, glm::vec3 point, float* outNearestT = nullptr);

// Real, pure velocity computation: real std::nullopt (no effect) when
// `playerPosition` is farther than `triggerRadius` from the real curve.
// Otherwise a real velocity of magnitude `travelSpeed`, directed along
// the curve's own real tangent at the nearest point -- **bidirectional**:
// the real sign is chosen by which endpoint `playerPosition` is closer
// to (ride *away* from the nearer end, toward the farther one), so
// approaching from either real end automatically rides the correct real
// direction with no separate "reverse" zip-line needed. A caller
// (Application's own live tick) applies this directly as the player's
// own horizontal velocity every tick they stay in range, the real,
// continuous "riding the line" feel, rather than a one-shot impulse.
[[nodiscard]] std::optional<glm::vec3> computeZipLineVelocity(const ZipLineState& zipLine, glm::vec3 playerPosition);

// Real, live-reported bug this table fixes: computeZipLineVelocity() above
// only ever sets a rider's *direction* (the curve's own tangent) --
// nothing about a per-tick velocity ever pulls a rider's actual position
// back onto the real curve. A rider who grabs on even slightly off the
// true arc (real and common: triggerRadius is a real 3D sphere around
// the *whole* curve, not a single point on it) rides in a real straight
// line parallel to their initial tangent forever, never curving --
// exactly "hooks onto the zip-line but rides the chord, not the cable."
// The real fix is arc-length parameterization: advanceZipLineRider()
// below sets a rider's position *directly and exactly on* the real curve
// every tick, eliminating the drift by construction instead of trying to
// correct it after the fact.
//
// This table holds kSampleCount+1 real cumulative distances from t=0 up
// to t=i/kSampleCount (Delta-t = 0.01, i.e. kSampleCount=100) -- built
// once per zip-line (its own shape never changes after spawn) so
// per-tick traversal can convert a desired travel *distance* into the
// correct real curve parameter t without re-integrating the curve every
// frame.
struct ZipLineArcLengthTable {
    static constexpr int kSampleCount = 100; // Delta-t = 0.01
    std::array<float, kSampleCount + 1> cumulativeDistance{};
    float totalLength = 0.0f;
};

// Real, pure table build -- samples sampleZipLineCurve() at kSampleCount+1
// evenly-spaced t values and accumulates the real real-world-unit
// distance between consecutive samples.
[[nodiscard]] ZipLineArcLengthTable buildZipLineArcLengthTable(const ZipLineState& zipLine);

// Real distance -> t conversion via linear interpolation between the two
// bracketing table samples -- the real inverse of arc-length
// parameterization: a constant real distance step every tick produces
// constant real speed *along the actual curve*, not the chord. Clamps to
// [0, table.totalLength] -- a distance outside that real range clamps to
// t=0/t=1 rather than extrapolating.
[[nodiscard]] float zipLineDistanceToT(const ZipLineArcLengthTable& table, float distance);

// Real t -> distance conversion -- the real counterpart to
// zipLineDistanceToT() above, used to seed a rider's own real
// distanceTraveled from a coarse nearest-point search's own t (see
// advanceZipLineRider()'s own caller-side use in Application.cpp).
[[nodiscard]] float zipLineTToDistance(const ZipLineArcLengthTable& table, float t);

// Real per-rider traversal state -- how far along the curve (in real
// world-unit distance, not t) this specific rider has traveled, and
// which real direction they're riding (matches computeZipLineVelocity()'s
// own real bidirectional convention: +1 rides start->end, -1 rides
// end->start).
struct ZipLineRiderState {
    float distanceTraveled = 0.0f;
    float direction = 1.0f;
};

// Real world units of vertical clearance a rider's own position is
// offset above the mathematical curve -- without this a rider's capsule
// (which has real physical radius) would clip into the real cable mesh/
// terrain directly below the curve's own centerline.
constexpr float kZipLineVerticalClearance = 0.75f;

// Real, direct position-based traversal -- the real fix described in
// ZipLineArcLengthTable's own header comment. Advances
// `rider.distanceTraveled` by `zipLine.travelSpeed * dt * rider.direction`,
// clamps it to [0, table.totalLength], converts it to the real curve
// parameter t via `table`, and returns
// `sampleZipLineCurve(zipLine, t) + up * kZipLineVerticalClearance` --
// the real, exact point on the curve a caller should place the rider at
// this tick (e.g. via Physics::setPosition()), not a velocity for
// physics to integrate. Real std::nullopt only for a real, degenerate
// zero-length table (nothing to traverse) -- a caller detects "ride
// complete" by checking whether `rider.distanceTraveled` has real-reached
// 0 or table.totalLength after this call, not by this return value.
[[nodiscard]] std::optional<glm::vec3> advanceZipLineRider(const ZipLineState& zipLine,
                                                              const ZipLineArcLengthTable& table,
                                                              ZipLineRiderState& rider, float dt);

// --- Movement boosts --------------------------------------------------

// Real, timed speed multiplier -- the same real "decaying/counting-down
// state a per-tick tick() function advances" shape
// tntwars::GameplayShakeState already established, applied here to move
// speed instead of camera trauma.
struct SpeedBoostState {
    float secondsRemaining = 0.0f;
    float multiplier = 1.0f; // real-1.0 (no effect) while inactive
};

constexpr float kDefaultBoostMultiplier = 1.8f;
constexpr float kDefaultBoostDurationSeconds = 4.0f;

// Real, direct (not additive/stacking) boost application -- a fresh
// pickup real-overwrites any still-active boost with the real new
// multiplier/duration rather than stacking multipliers, the same
// "simple, predictable, no runaway compounding" choice
// core::triggerFlash()'s own real "restarts, if already flashing"
// behavior already makes for a re-triggered flash.
void applySpeedBoost(SpeedBoostState& state, float multiplier = kDefaultBoostMultiplier,
                      float durationSeconds = kDefaultBoostDurationSeconds);
// Real per-tick countdown -- real-resets multiplier back to 1.0 the
// exact moment secondsRemaining first reaches 0.
void tickSpeedBoost(SpeedBoostState& state, float dt);
[[nodiscard]] bool isSpeedBoostActive(const SpeedBoostState& state);

} // namespace engine::tntwars
