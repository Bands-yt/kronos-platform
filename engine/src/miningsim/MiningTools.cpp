#include "miningsim/MiningTools.hpp"

namespace engine::miningsim {

const char* miningToolName(MiningToolType tool) {
    switch (tool) {
        case MiningToolType::Pickaxe: return "Pickaxe";
        case MiningToolType::Drill: return "Drill";
        case MiningToolType::Laser: return "Laser";
        case MiningToolType::PlasmaCutter: return "Plasma Cutter";
        case MiningToolType::SonicResonator: return "Sonic Resonator";
        case MiningToolType::GravityHammer: return "Gravity Hammer";
        case MiningToolType::ExplosiveCharge: return "Explosive Mining Charges";
        case MiningToolType::HydroDrill: return "Hydro-Drill";
        case MiningToolType::VoidExtractor: return "Void Extractor";
        case MiningToolType::HeavenlyChisel: return "Heavenly Chisel";
    }
    return "Pickaxe";
}

MiningToolStats miningToolStatsFor(MiningToolType tool) {
    switch (tool) {
        case MiningToolType::Pickaxe: return MiningToolStats{10, 1.0f, 1.0f};
        case MiningToolType::Drill: return MiningToolStats{18, 0.8f, 1.0f};
        case MiningToolType::Laser: return MiningToolStats{25, 0.6f, 0.9f};
        case MiningToolType::PlasmaCutter: return MiningToolStats{35, 0.7f, 1.1f};
        case MiningToolType::SonicResonator: return MiningToolStats{15, 0.5f, 1.3f};
        case MiningToolType::GravityHammer: return MiningToolStats{50, 1.5f, 1.0f};
        case MiningToolType::ExplosiveCharge: return MiningToolStats{80, 3.0f, 0.8f};
        case MiningToolType::HydroDrill: return MiningToolStats{20, 0.9f, 1.2f};
        case MiningToolType::VoidExtractor: return MiningToolStats{30, 1.0f, 1.5f};
        case MiningToolType::HeavenlyChisel: return MiningToolStats{22, 0.7f, 1.4f};
    }
    return MiningToolStats{};
}

std::optional<ZoneType> requiredZoneFor(MiningToolType tool) {
    switch (tool) {
        case MiningToolType::HydroDrill: return ZoneType::Underwater;
        case MiningToolType::VoidExtractor: return ZoneType::Void;
        case MiningToolType::HeavenlyChisel: return ZoneType::Heavenly;
        default: return std::nullopt;
    }
}

bool isToolEffectiveInZone(MiningToolType tool, ZoneType zone) {
    auto required = requiredZoneFor(tool);
    return !required.has_value() || *required == zone;
}

} // namespace engine::miningsim
