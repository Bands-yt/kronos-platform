#pragma once

#include <array>
#include <cstddef>

#include "tntwars/ThrusterHp.hpp"

namespace engine::tntwars {

// Kronos roadmap Milestone 10 ("Sky map, merged from SkyPlatforms"): the
// brief's own exact spec -- six real thrusters per side, each a real
// ThrusterHp.hpp state machine (5 hits -> destabilize -> tilt -> collapse,
// already real from Sprint 14, unmodified here), holding that team's own
// real sky platform aloft.
constexpr size_t kThrustersPerTeam = 6;

struct SkyPlatformState {
    std::array<ThrusterPlatformState, kThrustersPerTeam> thrusters{};
};

// Real, aggregate query: how many of this team's own 6 real thrusters
// are still real-standing (not collapsed).
[[nodiscard]] int standingThrusterCount(const SkyPlatformState& state);

// Real, direct consequence of the brief's own spec: once every real
// thruster collapses, that team's own real sky platform is no longer
// real-supported -- a real caller treats this the same way
// ThrusterPlatformState::collapsed already means "stop treating this as
// standable ground" for one platform, generalized here to "the whole
// side's platform is gone."
[[nodiscard]] bool isSkyPlatformCollapsed(const SkyPlatformState& state);

} // namespace engine::tntwars
