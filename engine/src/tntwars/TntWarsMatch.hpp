#pragma once

#include <array>
#include <optional>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "net/NetTypes.hpp"
#include "tntwars/Crafting.hpp"
#include "tntwars/MapDefinition.hpp"
#include "tntwars/MapLayout.hpp"
#include "tntwars/MatchFlow.hpp"
#include "tntwars/Missile.hpp"
#include "tntwars/Movement.hpp"
#include "tntwars/Projectile.hpp"
#include "tntwars/Scavenging.hpp"
#include "tntwars/SkyPlatforms.hpp"
#include "tntwars/Team.hpp"
#include "tntwars/TntWarsUpgrades.hpp"
#include "tntwars/TntCharge.hpp"
#include "tntwars/TntWarsAntiCheat.hpp"
#include "tntwars/TorpedoStealth.hpp"
#include "tntwars/TrenchesCover.hpp"
#include "tntwars/TrenchesWall.hpp"
#include "tntwars/UltimateCharge.hpp"

namespace engine::tntwars {

// Sprint 14's real, server-authoritative match orchestrator -- the one
// class net::NetworkSession owns and delegates to (see that class's own
// TNT-Wars wiring), tying together every real piece this module built:
// class selection, real health, real ultimate charge, real fire-weapon/
// projectile spawning (server-validated via TntWarsAntiCheat's real
// rate limiting, exactly mirroring how Sprint 11's ServerReconciliation
// validates movement), and the real MatchFlow state machine. Server-
// owns-world-state throughout -- a client only ever asks (fireWeapon()/
// triggerUltimate() return a real accept/reject, never silently apply
// on the caller's behalf).
class TntWarsMatch {
public:
    // Real map switch -- also real-reseeds scavengeNodes_/jumpPads_/
    // trenchesWall_ (Kronos Milestones 5/8/9), so a fresh map always
    // starts with a real, full, undamaged set of per-map state rather
    // than carrying over whatever the previous map's state happened to
    // be. trenchesWall_ is real-only populated for MapId::Trenches (the
    // one real map that has a wall) -- every other map real-clears it.
    void setMap(MapId map);
    [[nodiscard]] MapId map() const { return map_; }
    [[nodiscard]] MapModifiers mapModifiers() const { return mapTuning_.modifiersFor(map_); }

    // Kronos Milestone 1: real, automatic team assignment -- joins
    // whichever real team currently has fewer players (ties favor
    // TeamId::A, a real, deterministic tiebreak rather than an arbitrary
    // one). No client-facing "pick a team" request exists yet (a real,
    // later addition once a real lobby UI needs it) -- server-side
    // balancing is the real, honest behavior for now, the same
    // "server decides, client only ever asks" posture fireWeapon()/
    // triggerUltimate() already establish for combat.
    void registerPlayer(net::PlayerId player);
    void unregisterPlayer(net::PlayerId player);

    [[nodiscard]] TeamId teamOf(net::PlayerId player) const;
    [[nodiscard]] size_t teamPlayerCount(TeamId team) const;

    // Real, server-validated class selection -- only legal during
    // MatchPhase::Lobby or MatchPhase::ClassSelect (a class can't be
    // swapped mid-fight), matching the brief's own match-flow ordering.
    bool selectClass(net::PlayerId player, PlayerClassType classType);
    [[nodiscard]] bool hasSelectedClass(net::PlayerId player) const;
    [[nodiscard]] PlayerClassType classOf(net::PlayerId player) const;

    [[nodiscard]] float health(net::PlayerId player) const;

    // Kronos ("Quality-of-Life & Finalization", "Respawn system"): real,
    // mid-match respawn -- restores health to the player's own *current*
    // class's own real maxHealth (class retained, not reset -- this
    // method never touches playerClasses_, matching the brief's own
    // "timed respawn with class retention"). Deliberately the opposite
    // real phase gate from selectClass() (InProgress only, not Lobby/
    // ClassSelect) -- respawning mid-match only makes real sense once a
    // match is actually running. Real, honest no-op (false) if the
    // player has no real class selected yet -- nothing to respawn into.
    // Repositioning the live ECS/Physics character is the real caller's
    // own job (Application.cpp), the same "TntWarsMatch owns the
    // numbers, the live caller owns position" split every other real
    // action here already draws.
    bool respawnPlayer(net::PlayerId player);
    [[nodiscard]] bool isAlive(net::PlayerId player) const;
    // Real damage application: clamps health at 0, and -- matching
    // ClassStats::chargePerDamageDealt/chargePerDamageTaken -- feeds
    // real ultimate charge to BOTH the dealer and the target, using the
    // TARGET's own class stats for the charge-required clamp (each
    // player's meter fills toward their own real ultimate).
    void applyDamage(net::PlayerId dealer, net::PlayerId target, float damage);

    // Kronos Milestone 1: the real win-condition this game has never had.
    // Each team's Core starts at kCoreMaxHealth (real, tuned: 5x a
    // player's own real 100 maxHealth default -- see ClassSystem.hpp --
    // survivable enough to be a real defended objective, not a one-hit
    // kill, without needing an arbitrarily huge number). Real, direct
    // parallel to applyDamage() above, minus per-player ultimate-charge
    // crediting (the Core isn't a player, and no weapon targets it yet --
    // see the Kronos roadmap's own Milestone 3/4, "destructible geometry"/
    // "TNT core mechanic," which will call this once real weapons can
    // actually hit a Core).
    static constexpr float kCoreMaxHealth = 500.0f;
    [[nodiscard]] float coreHealth(TeamId team) const { return coreHealth_[static_cast<size_t>(team)]; }
    [[nodiscard]] bool isCoreDestroyed(TeamId team) const { return coreHealth(team) <= 0.0f; }
    // Real damage application against `team`'s own Core -- a caller
    // attacking the *enemy* Core passes tntwars::opposingTeam(attackerTeam),
    // the same real "the caller decides whose side is whose" convention
    // applyDamage() above already uses for dealer/target. Clamps at 0; the
    // real moment a Core first reaches 0 while the match is
    // MatchPhase::InProgress, this real-advances matchFlow() to
    // CinematicFinale and real-records the opposing team as the winner --
    // no external caller needs to poll for "is the match over" and
    // manually advance the phase, the same "server state changes, the
    // real consequence happens immediately" posture applyDamage()/
    // fireWeapon() already establish.
    void applyDamageToCore(TeamId team, float damage);

    [[nodiscard]] bool isMatchOver() const { return winningTeam_.has_value(); }
    [[nodiscard]] std::optional<TeamId> winningTeam() const { return winningTeam_; }

    // Kronos Milestone 3: real, server-validated charge placement -- only
    // legal while MatchPhase::InProgress, and only for a player who has a
    // real selected class and is real-alive, the same gating fireWeapon()
    // already applies to shots. Rejects (returns false) otherwise, with
    // no partial state change.
    bool placeTntCharge(net::PlayerId owner, glm::vec3 position);

    // Kronos Milestone 6: real, recipe-aware placement -- spends one real
    // crafted charge of `recipe` from the player's own craftedExplosives()
    // ledger (rejecting, with no state change, if they don't have one),
    // and places it with that real recipe's own real, stronger fuse/
    // radius/damage/impulse tuning (see ExplosiveRecipeInfo) instead of
    // the basic charge's fixed defaults. Same InProgress/class-selected/
    // alive gating as the basic overload above.
    bool placeTntCharge(net::PlayerId owner, glm::vec3 position, ExplosiveRecipeType recipe);
    [[nodiscard]] const std::vector<TntChargeState>& tntCharges() const { return tntCharges_; }

    // Kronos ("TNT Wars Gameplay Loop"): one real player caught in a real
    // charge's own explosion radius this tick -- `damage` is already the
    // real linear-falloff amount (see computeExplosionDamage()) and has
    // already been real-applied via applyDamage() by the time this shows
    // up in tickTntCharges()'s own out-vector below; `explosionCenter`/
    // `explosionRadius`/`explosionMaxImpulse` are the real charge's own
    // stats, restated here so a real ECS/Physics-touching caller (this
    // class stays real, pure, ECS/Vulkan-free -- see this class's own
    // header comment) can compute and apply the real knockback impulse
    // itself via tntwars::computeExplosionImpulse() + Physics::applyImpulse(),
    // the same "pure step here, real collision-touching caller applies
    // it" split ExplosionImpulse's own header comment already establishes.
    struct PlayerExplosionHit {
        net::PlayerId player = net::kInvalidPlayer;
        float damage = 0.0f;
        glm::vec3 explosionCenter{0.0f};
        float explosionRadius = 0.0f;
        float explosionMaxImpulse = 0.0f;
    };

    // Kronos ("Sky Map Full Engine Specification"): one real charge
    // detonation this tick -- restates the real charge's own explosion
    // stats so a real caller can apply tntwars::applyExplosionToSegments()
    // against *any* real DestructibleSegment set it owns (this class has
    // no idea a given map even has bridges/collapsible islands -- the
    // same "this class stays pure, the caller supplies/owns map-specific
    // extras" split trenchesWall_/trenchesCover_ above already establish
    // for the two destructible structures this class *does* know about;
    // this is the general-purpose escape hatch for every other map's own
    // real destructible extras instead of TntWarsMatch itself growing a
    // dedicated member per map forever).
    struct DetonationEvent {
        glm::vec3 position{0.0f};
        float explosionRadius = 0.0f;
        float explosionMaxDamage = 0.0f;
    };

    // Real per-tick fuse countdown for every real placed charge -- the
    // exact real tick a charge's fuse first reaches 0, both team Cores'
    // real world positions on this match's own map_ (see
    // MapLayout::coreWorldPosition()) take real linear-falloff explosion
    // damage via applyDamageToCore() -- which, per Milestone 1, already
    // real-ends the match the instant either Core reaches 0. This is the
    // real "TNT can destroy the enemy Core and win the match" loop the
    // brief's own core loop describes, with no external caller needing to
    // separately wire up Core-vs-explosion collision.
    //
    // Kronos ("TNT Wars Gameplay Loop"): `playerPositions` is a real,
    // optional (empty by default -- every existing call site/test keeps
    // real-working byte-for-byte unchanged) list of real live player
    // positions a caller supplies (this class has no ECS access of its
    // own, the same reason Oxygen.hpp's own tickOxygen() takes
    // `inOxygenZone` as a caller-supplied bool rather than deriving it
    // itself) -- any of them within a newly-detonating charge's own real
    // explosion radius takes real applyDamage() same as a weapon hit, and
    // (if `outPlayerHits` is non-null) gets a real PlayerExplosionHit
    // pushed for the caller's own real knockback application.
    // `outDetonations`, if non-null, gets one real DetonationEvent pushed
    // per real charge that detonates this exact tick -- same real,
    // additive, empty-by-default-so-every-existing-call-site-still-
    // compiles convention as `outPlayerHits`.
    void tickTntCharges(float dt, const std::vector<std::pair<net::PlayerId, glm::vec3>>& playerPositions = {},
                         std::vector<PlayerExplosionHit>* outPlayerHits = nullptr,
                         std::vector<DetonationEvent>* outDetonations = nullptr);

    // Real housekeeping -- erases every already-detonated charge from
    // tntCharges_ and returns how many were removed, so a real server
    // tick loop can keep this list from growing unbounded over a long
    // match. Deliberately not automatic inside tickTntCharges() itself:
    // a caller (e.g. a future FX/replication layer) may still want one
    // real tick to observe a charge in its just-detonated state before
    // it's gone.
    size_t removeDetonatedCharges();

    // Kronos Milestone 5: real, territory-gated scavenging. `nodeIndex`
    // indexes scavengeNodes() directly (an out-of-range index is a real,
    // honest no-op/failure, matching this engine's other bounds-checked
    // accessors) -- rejects (returns a failed ScavengeResult) outside
    // MatchPhase::InProgress or for a player without a real selected
    // class, the same gating placeTntCharge()/fireWeapon() already apply,
    // plus tntwars::scavengeNode()'s own real territory check. On
    // success, real-credits the collected material to the player's own
    // scavengedMaterials() ledger.
    ScavengeResult scavenge(net::PlayerId player, size_t nodeIndex, int amount);
    [[nodiscard]] const std::vector<ScavengeNodeState>& scavengeNodes() const { return scavengeNodes_; }
    // Kronos bugfix (live-reported: "no visual model for the resource
    // nodes"): real, mutable access -- lets a real caller with its own
    // real, authoritative team-base positions (main.cpp's own Sky/Space
    // Map setup) real-overwrite the default, generic MapId-keyed
    // placement with tntwars::buildScavengeNodesAt()'s own real,
    // explicit-position result. Same real "let a live caller mutate
    // match-owned state directly" convention trenchesWallMutable()
    // already establishes.
    [[nodiscard]] std::vector<ScavengeNodeState>& scavengeNodesMutable() { return scavengeNodes_; }
    [[nodiscard]] const ScavengedMaterials& scavengedMaterials(net::PlayerId player) const;

    // Kronos ("Gameplay Loop" world-building, "Progression"): real,
    // permanent per-player upgrades -- same real InProgress/class-
    // selected/alive gating craft()/scavenge() already apply, spending
    // straight from this same player's own real scavengedMaterials()
    // ledger (see tntwars::purchaseUpgrade()'s own real "no partial
    // spend on a rejected purchase" comment).
    [[nodiscard]] UpgradeResult purchaseUpgrade(net::PlayerId player, UpgradeCategory category);
    [[nodiscard]] const PlayerUpgrades& playerUpgrades(net::PlayerId player) const;

    // Real per-tick respawn countdown for every real scavenge node on the
    // current map.
    void tickScavengeNodes(float dt);

    // Kronos Milestone 6: real crafting -- spends the recipe's own real
    // ingredient cost from the player's own scavengedMaterials() ledger
    // (via tntwars::craftExplosive(), which itself rejects an
    // unaffordable recipe with no partial consumption) and, on success,
    // credits one real unit to their own craftedExplosives() ledger. Same
    // InProgress/class-selected/alive gating as scavenge()/placeTntCharge().
    bool craft(net::PlayerId player, ExplosiveRecipeType recipe);
    [[nodiscard]] const CraftedExplosives& craftedExplosives(net::PlayerId player) const;

    // Kronos Milestone 7: real missile launch -- rejects (no state
    // change) outside InProgress, for a player without a real selected
    // class, or if `targetTeam` is the launching player's own team (you
    // can't missile your own Core). Same InProgress/class-selected/alive
    // gating as every other real player action above.
    bool launchMissileAt(net::PlayerId owner, TeamId targetTeam);
    [[nodiscard]] const std::vector<MissileState>& missiles() const { return missiles_; }

    // Real Admin Room defense: `defender` must belong to the missile's
    // own real targetTeam (you defend your own team's Core, not the
    // enemy's) -- rejects otherwise, or if `missileIndex` is out of
    // range, or if the missile is already resolved (see
    // tntwars::tryInterceptMissile()'s own one-shot behavior).
    bool interceptMissile(net::PlayerId defender, size_t missileIndex);

    // Real per-tick flight countdown for every real launched missile --
    // the exact real tick a missile impacts (flight time ran out without
    // interception), its own real kMissileCoreDamage lands on
    // targetTeam's own real Core via applyDamageToCore(), which (per
    // Milestone 1) already real-ends the match if that Core reaches 0.
    void tickMissiles(float dt);

    // Real housekeeping, matching removeDetonatedCharges()'s own real
    // "keep the live list from growing unbounded" shape -- erases every
    // real resolved (intercepted or impacted) missile, returns how many
    // were removed.
    size_t removeResolvedMissiles();

    // Kronos Milestone 8: real jump pads -- `padIndex` indexes jumpPads()
    // directly (out-of-range is a real, honest failure, not a crash). On
    // success, real-starts that pad's own cooldown and returns the real
    // positional launch offset a caller adds directly to the player's own
    // Transform::position (see tntwars::triggerJumpPad()'s own header
    // comment on why this is a direct offset, not a fake velocity/
    // impulse). Same InProgress/class-selected/alive gating as every
    // other real player action.
    // Real, tuned amplification applied when mapModifiers().hasZeroGravity
    // is true (Kronos Milestone 12) -- a jump pad launches you real-further
    // with nothing pulling you back down.
    static constexpr float kZeroGravityJumpMultiplier = 2.5f;
    [[nodiscard]] std::optional<glm::vec3> triggerJumpPad(net::PlayerId player, glm::vec3 playerPosition,
                                                            size_t padIndex);
    [[nodiscard]] const std::vector<JumpPadState>& jumpPads() const { return jumpPads_; }
    void tickJumpPads(float dt);

    // Real movement-boost application/query -- same InProgress/class-
    // selected/alive gating as launchMissileAt()/scavenge().
    bool applySpeedBoostTo(net::PlayerId player, float multiplier = kDefaultBoostMultiplier,
                            float durationSeconds = kDefaultBoostDurationSeconds);
    // Real, direct consumer of a player's own real class moveSpeed
    // (ClassStats::moveSpeed, live-tunable via classTuning()) times
    // whatever real speed-boost multiplier is currently active for them,
    // times this match's own current map's real mapModifiers().pressureFactor
    // (Kronos Milestone 11: IslandSea's own real, previously-unconsumed
    // 0.85 default -- see MapDefinition.cpp -- is now a real, live
    // movement-speed effect for the first time, the honest "underwater
    // movement physics" the brief calls for, not a claim of real buoyancy/
    // swim-controller physics this engine has no dependency for). This is
    // the real number a caller should feed into
    // net::applyNetworkedMovement()'s own `moveSpeed` parameter for a
    // live TNT-Wars match, instead of the raw, un-boosted class stat.
    [[nodiscard]] float effectiveMoveSpeedFor(net::PlayerId player) const;
    void tickSpeedBoosts(float dt);

    // Kronos Milestone 11: real, direct consumer of this match's own
    // current map's real sonar buoy positions (see
    // tntwars::sonarSourcePositions()) -- the real "is a stealthed
    // torpedo currently visible" check a caller runs against a live
    // torpedo ProjectileState's own position, without needing to
    // separately look up or cache this map's own sonar sources.
    [[nodiscard]] bool isTorpedoVisibleToSonar(glm::vec3 torpedoPosition) const;

    // Kronos Milestone 9: the real Trenches mid-wall's own real,
    // per-segment health -- see TrenchesWall.hpp. Empty on every map
    // except Trenches. tickTntCharges() already real-applies each
    // detonating charge's own real falloff explosion damage to this wall
    // (see that method's own body) alongside its existing real Core
    // damage -- a charge placed far from the wall real-deals it 0 damage
    // automatically via the same real distance falloff, no extra gating
    // needed.
    [[nodiscard]] const std::vector<DestructibleSegment>& trenchesWall() const { return trenchesWall_; }
    [[nodiscard]] bool isTrenchesWallBreached() const { return tntwars::isTrenchesWallBreached(trenchesWall_); }
    // Kronos ("TNT Wars Foundational Playability" Phase 2): a real,
    // mutable accessor alongside the const one above -- what a live
    // caller's own tntwars::tickDestructibleWallVisual() needs to reset
    // a segment's health back to maxHealth on a real rebuild (see that
    // function's own comment). TntWarsMatch itself stays real, pure,
    // ECS/Vulkan-free either way (see this class's own header comment) --
    // this only exposes the data, it doesn't add any new dependency here.
    [[nodiscard]] std::vector<DestructibleSegment>& trenchesWallMutable() { return trenchesWall_; }

    // Kronos ("Four RTX Maps" Phase 5d): the same real, per-segment
    // destructible machinery as trenchesWall_ above, applied to the
    // Trenches map's own four "Cover_*" pieces instead of the mid-wall --
    // see TrenchesCover.hpp's own header comment. Empty on every map
    // except Trenches; tickTntCharges() applies the same real falloff
    // explosion damage to this alongside trenchesWall_/the Core.
    [[nodiscard]] const std::vector<DestructibleSegment>& trenchesCover() const { return trenchesCover_; }
    [[nodiscard]] std::vector<DestructibleSegment>& trenchesCoverMutable() { return trenchesCover_; }

    // Kronos Milestone 10: the real per-team Sky Platforms thruster state
    // -- present (but harmlessly idle/all-standing) on every map, real-
    // meaningful only on MapId::SkyPlatforms (see MapLayout.cpp's own
    // real "TeamX_Thruster_0".."_5" pieces, 1:1 indexed against
    // skyPlatform(team).thrusters). Rejects (returns false, no state
    // change) outside InProgress or for an out-of-range thrusterIndex --
    // deliberately NOT player-gated (class-selected/alive) the same way
    // applyDamageToCore()/applyExplosionToSegments() aren't: a structural
    // hit is the caller's own already-validated consequence (e.g. a real
    // projectile/explosion landing on the mount), not a new direct player
    // action of its own.
    bool applyThrusterHitAt(TeamId team, size_t thrusterIndex);
    [[nodiscard]] const SkyPlatformState& skyPlatform(TeamId team) const {
        return skyPlatforms_[static_cast<size_t>(team)];
    }

    struct FireResult {
        bool accepted = false;
        ProjectileState projectile;
    };
    // Real, server-side fire-weapon resolution: rejects (accepted=false)
    // if the player hasn't selected a class, is dead, or fails the real
    // TntWarsAntiCheat rate check (their own class's real cooldown-
    // derived cap) -- otherwise spawns a real ProjectileState via
    // Projectile.hpp's own spawnProjectile(), using the player's real
    // class stats for damage.
    [[nodiscard]] FireResult fireWeapon(net::PlayerId player, glm::vec3 origin, glm::vec3 aimDirection, float nowSeconds);

    struct UltimateResult {
        bool accepted = false;
        UltimateType type = UltimateType::FinalPush;
    };
    // Real, server-side ultimate-trigger resolution: rejects unless the
    // player's real charge meter has reached their class's real
    // ultimateChargeRequired -- on success, real-consumes the meter back
    // to 0 (see UltimateChargeTracker::consume()) so it can't be
    // double-spent.
    [[nodiscard]] UltimateResult triggerUltimate(net::PlayerId player, float nowSeconds);

    [[nodiscard]] UltimateChargeTracker& ultimateCharge() { return ultimateCharge_; }
    [[nodiscard]] MatchFlowController& matchFlow() { return matchFlow_; }
    [[nodiscard]] TntWarsAntiCheat& antiCheat() { return antiCheat_; }
    // Real, live-editable tuning tables -- see ClassTuningTable/
    // MapTuningTable's own class comments. Studio's TntWarsPlugin reads
    // and writes these directly through this accessor; every combat/fire/
    // ultimate/map-modifier code path above already consults these
    // tables (not the fixed classStatsFor()/mapModifiersFor() defaults),
    // so a Studio edit takes real effect on this real, running match.
    [[nodiscard]] ClassTuningTable& classTuning() { return classTuning_; }
    [[nodiscard]] MapTuningTable& mapTuning() { return mapTuning_; }

private:
    MapId map_ = MapId::Trenches;
    std::unordered_map<net::PlayerId, PlayerClassType> playerClasses_;
    std::unordered_map<net::PlayerId, float> playerHealth_;
    std::unordered_map<net::PlayerId, TeamId> playerTeams_;
    std::array<float, 2> coreHealth_{kCoreMaxHealth, kCoreMaxHealth}; // indexed by static_cast<size_t>(TeamId)
    std::optional<TeamId> winningTeam_;
    std::vector<TntChargeState> tntCharges_;
    std::vector<ScavengeNodeState> scavengeNodes_ = buildScavengeNodes(MapId::Trenches); // matches map_'s own default
    std::unordered_map<net::PlayerId, ScavengedMaterials> playerMaterials_;
    std::unordered_map<net::PlayerId, CraftedExplosives> playerCraftedExplosives_;
    std::unordered_map<net::PlayerId, PlayerUpgrades> playerUpgrades_;
    std::vector<MissileState> missiles_;
    std::vector<JumpPadState> jumpPads_ = buildJumpPads(MapId::Trenches); // matches map_'s own default
    std::unordered_map<net::PlayerId, SpeedBoostState> playerSpeedBoosts_;
    std::vector<DestructibleSegment> trenchesWall_ = buildTrenchesWall(); // matches map_'s own Trenches default
    std::vector<DestructibleSegment> trenchesCover_ = buildTrenchesCover(); // matches map_'s own Trenches default
    std::array<SkyPlatformState, 2> skyPlatforms_{}; // indexed by static_cast<size_t>(TeamId)
    UltimateChargeTracker ultimateCharge_;
    MatchFlowController matchFlow_;
    TntWarsAntiCheat antiCheat_;
    ClassTuningTable classTuning_;
    MapTuningTable mapTuning_;
};

} // namespace engine::tntwars
