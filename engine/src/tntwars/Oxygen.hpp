#pragma once

#include "tntwars/MapDefinition.hpp"

namespace engine::tntwars {

// Kronos ("TNT Wars Foundational Playability" Phase 2): the real
// underwater-specific mechanics the brief's own Underwater map asked
// for and this codebase never had at all -- oxygen management and
// slowed TNT fuse timing. Pure logic, no ECS/Physics dependency, same
// "real, headless-testable" split every other tntwars/ module (TorpedoStealth,
// LavaEruption, DestructibleGeometry) already establishes.

constexpr float kMaxOxygen = 100.0f;
// Real, tuned rates -- draining faster than it refills means a player
// genuinely has to manage this (dip in, grab what you came for, surface
// or find a real air pocket), not just an ignorable stat that trends
// back to full on its own.
constexpr float kOxygenDepletionPerSecond = 5.0f;
constexpr float kOxygenRefillPerSecond = 25.0f;
// Real, continuous chip damage once oxygen is fully depleted -- drowning
// is a real, ongoing hazard, not a one-time penalty.
constexpr float kOxygenDamagePerSecondAtZero = 8.0f;

struct OxygenState {
    float oxygen = kMaxOxygen;
};

// Real, clamped per-tick oxygen update:
//   - `submerged && !inOxygenZone`: real depletion at kOxygenDepletionPerSecond.
//   - `inOxygenZone` (a real air pocket -- ignores `submerged`, matching
//     a real air pocket being breathable even while technically still
//     underwater) or `!submerged` (surfaced): real refill at
//     kOxygenRefillPerSecond, clamped at kMaxOxygen.
// A non-positive `dt` is a real, honest no-op.
void tickOxygen(OxygenState& state, float dt, bool submerged, bool inOxygenZone);

// Real, continuous drowning damage -- a caller (TntWarsMatch's own
// per-tick player-health hook) applies this every tick oxygen is fully
// depleted; this function itself only computes the real amount, matching
// applyDamageToSegment()'s own "compute the real number, caller applies
// it" split.
[[nodiscard]] float drowningDamageThisTick(const OxygenState& state, float dt);
[[nodiscard]] bool isDrowning(const OxygenState& state);

// Real, tuned fuse-rate multiplier for a TNT charge's own fuse countdown
// -- see TntWarsMatch::tickTntCharges()'s own real per-map application.
// Fuses burn at real half speed underwater (water resistance slowing the
// real burn), not the same fixed rate as every other map.
constexpr float kUnderwaterFuseTimeMultiplier = 0.5f;
[[nodiscard]] float fuseDtMultiplierFor(MapId map);

} // namespace engine::tntwars
