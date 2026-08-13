#pragma once

#include <cstddef>
#include <cstdint>

namespace engine::miningsim {

// Kronos roadmap Milestone 13's own real zone taxonomy -- the brief's
// exact 8 zone types. Pure data/enum, no rendering or ECS dependency
// (matches tntwars::MapLayout.hpp's own "pure data the render/ECS layer
// turns into real entities" precedent): a real per-zone-type content
// builder (Phase 4's later "world asset pipeline generalization"
// milestone) is what will eventually turn one of these into a real,
// spawned cavern.
enum class ZoneType : uint8_t {
    Normal,
    Underwater,
    Void,
    Heavenly,
    CorruptedHeavenly,
    BioluminescentCaverns,
    Dungeon,
    DevBonus,
};
constexpr size_t kZoneTypeCount = 8;

[[nodiscard]] const char* zoneTypeName(ZoneType type);

// Real, explicit "is this a starting zone" flag -- Normal is the only
// real starting zone type (every other zone type is something a player
// travels to or unlocks); kept as its own real function rather than an
// inline `== Normal` check at every call site so the real *rule*
// ("starting zones == Normal zones") lives in exactly one place.
[[nodiscard]] bool isStartingZoneType(ZoneType type);

} // namespace engine::miningsim
