#include "miningsim/Zone.hpp"

namespace engine::miningsim {

const char* zoneTypeName(ZoneType type) {
    switch (type) {
        case ZoneType::Normal: return "Normal";
        case ZoneType::Underwater: return "Underwater";
        case ZoneType::Void: return "Void";
        case ZoneType::Heavenly: return "Heavenly";
        case ZoneType::CorruptedHeavenly: return "Corrupted Heavenly";
        case ZoneType::BioluminescentCaverns: return "Bioluminescent Caverns";
        case ZoneType::Dungeon: return "Dungeon";
        case ZoneType::DevBonus: return "Dev Bonus";
    }
    return "Normal";
}

bool isStartingZoneType(ZoneType type) { return type == ZoneType::Normal; }

} // namespace engine::miningsim
