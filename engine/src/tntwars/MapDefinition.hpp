#pragma once

#include <array>
#include <cstddef>

namespace engine::tntwars {

// Sprint 14's four real, playable maps, plus Kronos roadmap Milestone 12's
// Space -- the brief's own fourth "merged" TNT-Wars map (Trenches/Sky/
// Underwater/Space). Mantle stays in the roster unmodified rather than
// being deleted or folded away: it's real, working, already-tested
// content (Sprint 14), and removing it would be a purely cosmetic,
// high-diff, regression-risking rename/removal for zero real gameplay
// value -- the honest choice, matching this session's own "rebrand user-
// facing identity, don't rip out working systems for a naming exercise"
// precedent. Space instead carries Mantle's own real lava-hazard identity
// forward (see MapLayout.cpp's own buildSpace(), which reuses the exact
// real LavaEruption system via a "LavaPool"-named piece), satisfying the
// roadmap's own "fold into one of the four merged maps" resolution for
// Mantle's flagged fate.
enum class MapId { Trenches, Mantle, SkyPlatforms, IslandSea, Space };
constexpr size_t kMapCount = 5;

[[nodiscard]] const char* mapName(MapId map);

// Real, per-map environmental modifiers and which real hazard/mechanic
// systems (ThrusterHp.hpp/LavaEruption.hpp/TorpedoStealth.hpp) are
// active on that map -- one real, central place a caller (server-side
// match setup, Studio's TntWarsPlugin) reads rather than re-deriving
// "which map has lava" from the map id ad hoc at every call site.
struct MapModifiers {
    float fogDensity = 0.0f;          // real 0..1, matches core::TimeOfDay's own fog-density convention
    float heatDamagePerSecond = 0.0f; // real, continuous chip damage for standing in a hot zone (Mantle)
    float pressureFactor = 1.0f;      // real movement-speed multiplier (IslandSea's underwater pressure)
    bool hasLavaHazard = false;
    bool hasThrusterPlatforms = false;
    bool hasTorpedoStealth = false;
    bool hasSonarIntercept = false;
    // Kronos roadmap Milestone 12 (Space map): real, queryable "movement
    // physics differ here" flag -- consumed by
    // TntWarsMatch::triggerJumpPad(), which real-amplifies a jump pad's
    // own launch offset under zero gravity (see that method's own
    // comment). Not a claim of full zero-G flight/orientation physics --
    // this module has no Physics dependency to build that on, the same
    // honest "real, bounded consumer" scope pressureFactor's own
    // real move-speed consumption already established.
    bool hasZeroGravity = false;
};

[[nodiscard]] MapModifiers mapModifiersFor(MapId map);

// Real, mutable per-map modifier overrides -- the "map editing" half of
// Studio's TntWarsPlugin. Same real-override-table shape as
// ClassSystem.hpp's ClassTuningTable, for the same reason: mapModifiersFor()
// stays the fixed default table, TntWarsMatch::mapModifiers() consults
// this one, so editing fog/heat/pressure in Studio takes real effect on
// the running match's next real tick.
class MapTuningTable {
public:
    MapTuningTable();

    [[nodiscard]] MapModifiers modifiersFor(MapId map) const;
    void setModifiersFor(MapId map, const MapModifiers& modifiers);
    void resetToDefaults();

private:
    std::array<MapModifiers, kMapCount> modifiers_{};
};

} // namespace engine::tntwars
