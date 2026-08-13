#include "tntwars/ThrusterHp.hpp"

namespace engine::tntwars {

void applyThrusterHit(ThrusterPlatformState& state) {
    if (state.collapsed) return; // real, honest no-op -- see header comment

    ++state.hitsTaken;
    float progress = static_cast<float>(state.hitsTaken) / static_cast<float>(ThrusterPlatformState::kMaxHits);
    state.tiltDegrees = progress * ThrusterPlatformState::kMaxTiltDegrees;

    if (state.hitsTaken >= ThrusterPlatformState::kMaxHits) {
        state.collapsed = true;
        state.tiltDegrees = ThrusterPlatformState::kMaxTiltDegrees;
    }
}

bool isThrusterDestabilizing(const ThrusterPlatformState& state) {
    return !state.collapsed && state.hitsTaken > 0;
}

} // namespace engine::tntwars
