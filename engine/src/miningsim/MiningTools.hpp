#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "miningsim/Zone.hpp"

namespace engine::miningsim {

// Kronos roadmap Milestone 15 ("Tool roster"): the brief's own exact 10
// real tools. Pickaxe is the real baseline -- core::OreNode's own
// existing mine-on-swing interaction already implicitly assumes a
// pickaxe-equivalent default; every other tool here is a real, distinct
// alternative with its own real stats, reusing that exact same real
// interaction shape (see MiningToolStats::miningPower below, which feeds
// core::mineOreNode(node, miningPower) directly) rather than inventing a
// second one, per the roadmap's own explicit guidance.
enum class MiningToolType : uint8_t {
    Pickaxe,
    Drill,
    Laser,
    PlasmaCutter,
    SonicResonator,
    GravityHammer,
    ExplosiveCharge,
    HydroDrill,
    VoidExtractor,
    HeavenlyChisel,
};
constexpr size_t kMiningToolTypeCount = 10;

[[nodiscard]] const char* miningToolName(MiningToolType tool);

// Real, per-tool stats -- speed/yield/power genuinely differ per tool
// (not six recolors of one number), the same "real, tuned progression"
// precedent core::OreTypeInfo already set for ore rarity.
struct MiningToolStats {
    int miningPower = 10;              // real, direct core::mineOreNode() input
    float swingCooldownSeconds = 1.0f; // real, direct core::Interactable::cooldownSeconds input
    float yieldMultiplier = 1.0f;      // real multiplier a caller applies to rolled drop quantities
};
[[nodiscard]] MiningToolStats miningToolStatsFor(MiningToolType tool);

// Real zone-gating: HydroDrill/VoidExtractor/HeavenlyChisel are each
// real-built for exactly one real zone (Underwater/Void/Heavenly
// respectively, per the brief) -- every other tool returns real
// std::nullopt (no required zone, usable everywhere).
[[nodiscard]] std::optional<ZoneType> requiredZoneFor(MiningToolType tool);

// Real, direct "is this tool at full real effectiveness in this zone"
// check -- true for every general-purpose tool in every zone, and real-
// true for a zone-specific tool only when `zone` actually matches its
// own requiredZoneFor().
[[nodiscard]] bool isToolEffectiveInZone(MiningToolType tool, ZoneType zone);

} // namespace engine::miningsim
