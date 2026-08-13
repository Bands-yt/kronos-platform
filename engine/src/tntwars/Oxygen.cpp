#include "tntwars/Oxygen.hpp"

#include <algorithm>

namespace engine::tntwars {

void tickOxygen(OxygenState& state, float dt, bool submerged, bool inOxygenZone) {
    if (dt <= 0.0f) return;
    if (submerged && !inOxygenZone) {
        state.oxygen = std::max(0.0f, state.oxygen - kOxygenDepletionPerSecond * dt);
    } else {
        state.oxygen = std::min(kMaxOxygen, state.oxygen + kOxygenRefillPerSecond * dt);
    }
}

float drowningDamageThisTick(const OxygenState& state, float dt) {
    if (dt <= 0.0f || !isDrowning(state)) return 0.0f;
    return kOxygenDamagePerSecondAtZero * dt;
}

bool isDrowning(const OxygenState& state) { return state.oxygen <= 0.0f; }

float fuseDtMultiplierFor(MapId map) { return map == MapId::IslandSea ? kUnderwaterFuseTimeMultiplier : 1.0f; }

} // namespace engine::tntwars
