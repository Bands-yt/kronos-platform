#pragma once

#include "net/NetTypes.hpp"
#include "tntwars/Team.hpp"

namespace engine::tntwars {

// Kronos roadmap Milestone 7 ("Missiles + Admin Room defense"): a real,
// server-owned missile threat against a specific team's own Core --
// distinct from the generic, client-simulated tntwars::ProjectileState
// (see ClassSystem.hpp's own real ProjectileType::Missile, which exists
// for a real client's own visual/replication classification). A real
// Admin Room defense minigame needs the server to be the one
// authoritative source of "is this specific missile still live," exactly
// like it already is for a placed TNT charge (TntChargeState) rather
// than a player-fired shot (which every client simulates independently).
struct MissileState {
    net::PlayerId owner = net::kInvalidPlayer;
    TeamId targetTeam = TeamId::A; // real -- the team whose own Core this missile threatens
    float flightSecondsRemaining = 0.0f;
    bool intercepted = false;
    bool impacted = false; // real-true the exact moment flight time runs out without a real interception
};

constexpr float kMissileFlightSeconds = 6.0f; // real, tuned -- long enough for a real Admin Room defender to react
constexpr float kMissileCoreDamage = 120.0f; // real, tuned direct-hit damage against the target team's own Core

[[nodiscard]] MissileState launchMissile(net::PlayerId owner, TeamId targetTeam,
                                          float flightSeconds = kMissileFlightSeconds);

// Real, one-shot interception attempt -- the same real "must not already
// be resolved" shape tntwars::tryInterceptBlip() already established for
// RadarBlip, applied here to a missile: succeeds (sets `intercepted`)
// only while the missile is still real mid-flight (not yet
// impacted/intercepted).
bool tryInterceptMissile(MissileState& missile);

// Real per-tick flight countdown -- sets `impacted` true the exact real
// moment flightSecondsRemaining first reaches 0, unless the missile was
// already real-intercepted (a real, honest no-op past that point,
// matching tickTntCharge()'s own idempotency).
void tickMissile(MissileState& missile, float dt);

} // namespace engine::tntwars
