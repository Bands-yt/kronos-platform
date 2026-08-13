#include "tntwars/SkyPlatforms.hpp"

namespace engine::tntwars {

int standingThrusterCount(const SkyPlatformState& state) {
    int count = 0;
    for (const ThrusterPlatformState& thruster : state.thrusters) {
        if (!thruster.collapsed) ++count;
    }
    return count;
}

bool isSkyPlatformCollapsed(const SkyPlatformState& state) { return standingThrusterCount(state) == 0; }

} // namespace engine::tntwars
