#include "tntwars/ClassSystem.hpp"

#include <cstddef>
#include <cstring>

namespace engine::tntwars {

const char* playerClassName(PlayerClassType classType) {
    switch (classType) {
        case PlayerClassType::Striker: return "Striker";
        case PlayerClassType::Deflector: return "Deflector";
        case PlayerClassType::Engineer: return "Engineer";
        case PlayerClassType::Interceptor: return "Interceptor";
        case PlayerClassType::Saboteur: return "Saboteur";
    }
    return "Striker";
}

bool playerClassFromName(const char* name, PlayerClassType& out) {
    if (name == nullptr) return false;
    if (std::strcmp(name, "Striker") == 0) {
        out = PlayerClassType::Striker;
    } else if (std::strcmp(name, "Deflector") == 0) {
        out = PlayerClassType::Deflector;
    } else if (std::strcmp(name, "Engineer") == 0) {
        out = PlayerClassType::Engineer;
    } else if (std::strcmp(name, "Interceptor") == 0) {
        out = PlayerClassType::Interceptor;
    } else if (std::strcmp(name, "Saboteur") == 0) {
        out = PlayerClassType::Saboteur;
    } else {
        return false;
    }
    return true;
}

const char* projectileTypeName(ProjectileType type) {
    switch (type) {
        case ProjectileType::Rocket: return "Rocket";
        case ProjectileType::ShieldBolt: return "ShieldBolt";
        case ProjectileType::RepairBeam: return "RepairBeam";
        case ProjectileType::RadarPing: return "RadarPing";
        case ProjectileType::Torpedo: return "Torpedo";
        case ProjectileType::Missile: return "Missile";
    }
    return "Rocket";
}

const char* ultimateTypeName(UltimateType type) {
    switch (type) {
        case UltimateType::FinalPush: return "FinalPush";
        case UltimateType::BarrierBreak: return "BarrierBreak";
        case UltimateType::Overclock: return "Overclock";
        case UltimateType::HyperScan: return "HyperScan";
        case UltimateType::ShadowDive: return "ShadowDive";
    }
    return "FinalPush";
}

UltimateType ultimateForClass(PlayerClassType classType) {
    switch (classType) {
        case PlayerClassType::Striker: return UltimateType::FinalPush;
        case PlayerClassType::Deflector: return UltimateType::BarrierBreak;
        case PlayerClassType::Engineer: return UltimateType::Overclock;
        case PlayerClassType::Interceptor: return UltimateType::HyperScan;
        case PlayerClassType::Saboteur: return UltimateType::ShadowDive;
    }
    return UltimateType::FinalPush;
}

ProjectileType primaryProjectileForClass(PlayerClassType classType) {
    switch (classType) {
        case PlayerClassType::Striker: return ProjectileType::Rocket;
        case PlayerClassType::Deflector: return ProjectileType::ShieldBolt;
        case PlayerClassType::Engineer: return ProjectileType::RepairBeam;
        case PlayerClassType::Interceptor: return ProjectileType::RadarPing;
        case PlayerClassType::Saboteur: return ProjectileType::Torpedo;
    }
    return ProjectileType::Rocket;
}

ClassStats classStatsFor(PlayerClassType classType) {
    switch (classType) {
        case PlayerClassType::Striker:
            return ClassStats{110.0f, 5.0f, 28.0f, 1.6f, 100.0f, 1.0f, 0.5f};
        case PlayerClassType::Deflector:
            return ClassStats{160.0f, 4.5f, 10.0f, 0.8f, 120.0f, 0.8f, 0.6f};
        case PlayerClassType::Engineer:
            return ClassStats{120.0f, 6.0f, 8.0f, 0.6f, 90.0f, 0.6f, 0.5f};
        case PlayerClassType::Interceptor:
            return ClassStats{85.0f, 8.0f, 6.0f, 0.4f, 80.0f, 0.4f, 0.4f};
        case PlayerClassType::Saboteur:
            return ClassStats{95.0f, 4.0f, 35.0f, 2.2f, 110.0f, 1.2f, 0.5f};
    }
    return ClassStats{};
}

ClassTuningTable::ClassTuningTable() { resetToDefaults(); }

ClassStats ClassTuningTable::statsFor(PlayerClassType classType) const {
    return stats_[static_cast<size_t>(classType)];
}

void ClassTuningTable::setStatsFor(PlayerClassType classType, const ClassStats& stats) {
    stats_[static_cast<size_t>(classType)] = stats;
}

void ClassTuningTable::resetToDefaults() {
    for (size_t i = 0; i < stats_.size(); ++i) {
        stats_[i] = classStatsFor(static_cast<PlayerClassType>(i));
    }
}

} // namespace engine::tntwars
