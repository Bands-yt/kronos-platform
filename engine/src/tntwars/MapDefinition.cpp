#include "tntwars/MapDefinition.hpp"

#include <cstddef>

namespace engine::tntwars {

const char* mapName(MapId map) {
    switch (map) {
        case MapId::Trenches: return "Trenches";
        case MapId::Mantle: return "Mantle";
        case MapId::SkyPlatforms: return "Sky Platforms";
        case MapId::IslandSea: return "Island Sea";
        case MapId::Space: return "Space";
    }
    return "Trenches";
}

MapModifiers mapModifiersFor(MapId map) {
    switch (map) {
        case MapId::Trenches:
            // Classic artillery warfare -- deliberately the real
            // baseline map with no active hazard systems, so a new
            // player's first real match isn't also fighting the
            // environment.
            return MapModifiers{0.0f, 0.0f, 1.0f, false, false, false, false};
        case MapId::Mantle:
            return MapModifiers{0.15f, 2.0f, 1.0f, true, false, false, false};
        case MapId::SkyPlatforms:
            return MapModifiers{0.05f, 0.0f, 1.0f, false, true, false, false};
        case MapId::IslandSea:
            return MapModifiers{0.25f, 0.0f, 0.85f, false, false, true, true};
        case MapId::Space:
            // Real, reused lava-hazard identity (Mantle's own real
            // LavaEruption system, see MapLayout.cpp's buildSpace()) on
            // one of Space's two real planets, plus the real, new
            // hasZeroGravity flag TntWarsMatch::triggerJumpPad() consumes.
            return MapModifiers{0.05f, 2.0f, 1.0f, true, false, false, false, true};
    }
    return MapModifiers{};
}

MapTuningTable::MapTuningTable() { resetToDefaults(); }

MapModifiers MapTuningTable::modifiersFor(MapId map) const { return modifiers_[static_cast<size_t>(map)]; }

void MapTuningTable::setModifiersFor(MapId map, const MapModifiers& modifiers) {
    modifiers_[static_cast<size_t>(map)] = modifiers;
}

void MapTuningTable::resetToDefaults() {
    for (size_t i = 0; i < modifiers_.size(); ++i) {
        modifiers_[i] = mapModifiersFor(static_cast<MapId>(i));
    }
}

} // namespace engine::tntwars
