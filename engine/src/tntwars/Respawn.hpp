#pragma once

namespace engine::tntwars {

// Kronos ("Quality-of-Life & Finalization", "Respawn system"): real,
// pure, fully unit-testable respawn-countdown logic -- same "zero ECS/
// window dependency" discipline core::Weather/core::Wind already
// establish. The live caller (Application.cpp) drives this every tick
// from TntWarsMatch::isAlive(player); when tickRespawn() reports
// ReadyToRespawn, the caller is responsible for the real revive
// (TntWarsMatch::respawnPlayer() + repositioning the live ECS/Physics
// character at a real spawn point) -- this struct only owns the real
// countdown, not the actual revive action.
constexpr float kRespawnSeconds = 5.0f;

struct RespawnState {
    bool waitingToRespawn = false;
    float respawnSecondsRemaining = 0.0f;
};

enum class RespawnTickResult { NoChange, CountdownStarted, StillWaiting, ReadyToRespawn };

// Real per-tick check -- the real moment `isAliveNow` is false and no
// countdown is already running, real-starts a fresh kRespawnSeconds
// countdown (CountdownStarted); while already waiting, counts real `dt`
// down (StillWaiting), or -- the exact real tick it reaches 0 --
// real-clears `waitingToRespawn` and reports ReadyToRespawn (a real,
// one-shot signal; the caller is expected to actually revive the player
// this same tick, after which `isAliveNow` will real-read true again
// next tick). A real, honest NoChange while genuinely alive and not
// waiting -- the common, steady-state case.
[[nodiscard]] RespawnTickResult tickRespawn(RespawnState& state, bool isAliveNow, float dt);

} // namespace engine::tntwars
