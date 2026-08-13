#include "tntwars/Missile.hpp"

namespace engine::tntwars {

MissileState launchMissile(net::PlayerId owner, TeamId targetTeam, float flightSeconds) {
    MissileState missile;
    missile.owner = owner;
    missile.targetTeam = targetTeam;
    missile.flightSecondsRemaining = flightSeconds;
    return missile;
}

bool tryInterceptMissile(MissileState& missile) {
    if (missile.intercepted || missile.impacted) return false;
    missile.intercepted = true;
    return true;
}

void tickMissile(MissileState& missile, float dt) {
    if (missile.intercepted || missile.impacted || dt <= 0.0f) return;
    missile.flightSecondsRemaining -= dt;
    if (missile.flightSecondsRemaining <= 0.0f) {
        missile.flightSecondsRemaining = 0.0f;
        missile.impacted = true;
    }
}

} // namespace engine::tntwars
