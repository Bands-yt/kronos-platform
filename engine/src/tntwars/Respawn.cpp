#include "tntwars/Respawn.hpp"

namespace engine::tntwars {

RespawnTickResult tickRespawn(RespawnState& state, bool isAliveNow, float dt) {
    if (isAliveNow) {
        state.waitingToRespawn = false;
        return RespawnTickResult::NoChange;
    }

    if (!state.waitingToRespawn) {
        state.waitingToRespawn = true;
        state.respawnSecondsRemaining = kRespawnSeconds;
        return RespawnTickResult::CountdownStarted;
    }

    if (dt > 0.0f) state.respawnSecondsRemaining -= dt;
    if (state.respawnSecondsRemaining <= 0.0f) {
        state.waitingToRespawn = false;
        return RespawnTickResult::ReadyToRespawn;
    }

    return RespawnTickResult::StillWaiting;
}

} // namespace engine::tntwars
