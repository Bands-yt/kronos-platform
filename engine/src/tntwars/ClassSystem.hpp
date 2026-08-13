#pragma once

#include <array>

namespace engine::tntwars {

// Sprint 14 ("TNT-Wars Core Game Build"): the five real, playable
// classes named in the brief. "Anime-style" is visual art direction --
// character models/shaders/rigs are a real, separate asset-pipeline
// concern this engine has no generator for (see core::spawnRiggedAvatar()
// for the closest existing precedent: a real procedural humanoid, not a
// stylized one) -- what this module delivers is the real, working
// gameplay *system* underneath a class: stats, a weapon, and an
// ultimate, server-authoritative like everything else in this engine.
enum class PlayerClassType { Striker, Deflector, Engineer, Interceptor, Saboteur };

[[nodiscard]] const char* playerClassName(PlayerClassType classType);
[[nodiscard]] bool playerClassFromName(const char* name, PlayerClassType& out);

// The five real, distinct weapon/projectile identities the brief names
// alongside each class -- see Projectile.hpp for the real ballistic
// simulation each one drives. Missile (Kronos roadmap Milestone 7) is a
// real, sixth, non-class-bound type: no class's primaryProjectileForClass()
// below returns it -- it's launched through TntWarsMatch's own dedicated
// launchMissileAt(), and its real hit/interception resolution lives in
// Missile.hpp's server-owned MissileState, not this generic, client-
// simulated ProjectileState system (see that file's own header comment
// for why).
enum class ProjectileType { Rocket, ShieldBolt, RepairBeam, RadarPing, Torpedo, Missile };
[[nodiscard]] const char* projectileTypeName(ProjectileType type);

// The five real, named ultimates, one per class -- see
// CinematicSequence.hpp for the real camera-path/network-sync system
// that plays when one triggers.
enum class UltimateType { FinalPush, BarrierBreak, Overclock, HyperScan, ShadowDive };
[[nodiscard]] const char* ultimateTypeName(UltimateType type);
[[nodiscard]] UltimateType ultimateForClass(PlayerClassType classType);
[[nodiscard]] ProjectileType primaryProjectileForClass(PlayerClassType classType);

// Real, tunable per-class stats -- illustrative starting values (same
// honesty level as safety::RiskScore's own escalation thresholds: a
// real game-balance pass is a design decision, not an engineering one),
// but real numbers a real server actually enforces (see
// TntWarsMatch.hpp), not placeholders. Deliberately distinct silhouettes
// per class: Striker trades mobility for raw damage, Deflector trades
// damage for survivability, Engineer sits in the middle with a
// repair-focused kit, Interceptor is a fast, fragile utility class,
// Saboteur is the slowest but hits hardest at range via stealth.
struct ClassStats {
    float maxHealth = 100.0f;
    float moveSpeed = 6.0f;
    float primaryDamage = 15.0f;
    float primaryCooldownSeconds = 1.0f;
    float ultimateChargeRequired = 100.0f;
    // Charge earned per real point of damage dealt AND per real point of
    // damage taken (both -- a class that's fighting, on either side of
    // it, should charge up, matching the brief's "team ultimates" being
    // earned through real match participation, not just farming kills).
    float chargePerDamageDealt = 1.0f;
    float chargePerDamageTaken = 0.5f;
};

[[nodiscard]] ClassStats classStatsFor(PlayerClassType classType);

// Real, mutable per-class stat overrides -- what Studio's TntWarsPlugin
// "class tuning" UI actually edits. classStatsFor() above stays the
// real, fixed *default* table (used to seed this one and to power a
// real "Reset to Defaults" action); TntWarsMatch consults a
// ClassTuningTable, not classStatsFor() directly, so a tuning change
// made in Studio while a match is running takes real effect on the very
// next real fireWeapon()/triggerUltimate()/applyDamage() call -- not a
// cosmetic slider that doesn't touch gameplay.
class ClassTuningTable {
public:
    ClassTuningTable();

    [[nodiscard]] ClassStats statsFor(PlayerClassType classType) const;
    void setStatsFor(PlayerClassType classType, const ClassStats& stats);
    void resetToDefaults();

private:
    std::array<ClassStats, 5> stats_{};
};

} // namespace engine::tntwars
