#include "tntwars/TntWarsMatch.hpp"

#include <algorithm>

#include "tntwars/Oxygen.hpp"

namespace engine::tntwars {

void TntWarsMatch::setMap(MapId map) {
    map_ = map;
    scavengeNodes_ = buildScavengeNodes(map);
    jumpPads_ = buildJumpPads(map);
    trenchesWall_ = map == MapId::Trenches ? buildTrenchesWall() : std::vector<DestructibleSegment>{};
    trenchesCover_ = map == MapId::Trenches ? buildTrenchesCover() : std::vector<DestructibleSegment>{};
}

void TntWarsMatch::registerPlayer(net::PlayerId player) {
    playerHealth_[player] = classTuning_.statsFor(PlayerClassType::Striker).maxHealth;
    // Real, deterministic auto-balance -- see this method's own header
    // comment. teamPlayerCount() below reads playerTeams_ as it stood
    // *before* this player is added, so the very first player always
    // lands on TeamId::A (0 == 0, ties favor A).
    TeamId team = teamPlayerCount(TeamId::A) <= teamPlayerCount(TeamId::B) ? TeamId::A : TeamId::B;
    playerTeams_[player] = team;
}

void TntWarsMatch::unregisterPlayer(net::PlayerId player) {
    playerClasses_.erase(player);
    playerHealth_.erase(player);
    playerTeams_.erase(player);
    playerMaterials_.erase(player);
    playerCraftedExplosives_.erase(player);
    playerSpeedBoosts_.erase(player);
    ultimateCharge_.removePlayer(player);
    antiCheat_.removePlayer(player);
}

TeamId TntWarsMatch::teamOf(net::PlayerId player) const {
    auto it = playerTeams_.find(player);
    return it != playerTeams_.end() ? it->second : TeamId::A;
}

size_t TntWarsMatch::teamPlayerCount(TeamId team) const {
    return static_cast<size_t>(
        std::count_if(playerTeams_.begin(), playerTeams_.end(), [team](const auto& entry) { return entry.second == team; }));
}

bool TntWarsMatch::selectClass(net::PlayerId player, PlayerClassType classType) {
    if (matchFlow_.phase() != MatchPhase::Lobby && matchFlow_.phase() != MatchPhase::ClassSelect) return false;
    playerClasses_[player] = classType;
    playerHealth_[player] = classTuning_.statsFor(classType).maxHealth;
    return true;
}

bool TntWarsMatch::hasSelectedClass(net::PlayerId player) const { return playerClasses_.count(player) > 0; }

bool TntWarsMatch::respawnPlayer(net::PlayerId player) {
    if (matchFlow_.phase() != MatchPhase::InProgress) return false;
    if (!hasSelectedClass(player)) return false;
    playerHealth_[player] = classTuning_.statsFor(classOf(player)).maxHealth;
    return true;
}

PlayerClassType TntWarsMatch::classOf(net::PlayerId player) const {
    auto it = playerClasses_.find(player);
    return it != playerClasses_.end() ? it->second : PlayerClassType::Striker;
}

float TntWarsMatch::health(net::PlayerId player) const {
    auto it = playerHealth_.find(player);
    return it != playerHealth_.end() ? it->second : 0.0f;
}

bool TntWarsMatch::isAlive(net::PlayerId player) const { return health(player) > 0.0f; }

void TntWarsMatch::applyDamage(net::PlayerId dealer, net::PlayerId target, float damage) {
    if (damage <= 0.0f) return;
    float& targetHealth = playerHealth_[target];
    targetHealth = std::max(0.0f, targetHealth - damage);

    ClassStats dealerStats = classTuning_.statsFor(classOf(dealer));
    ClassStats targetStats = classTuning_.statsFor(classOf(target));
    ultimateCharge_.addCharge(dealer, damage * dealerStats.chargePerDamageDealt, dealerStats.ultimateChargeRequired);
    ultimateCharge_.addCharge(target, damage * targetStats.chargePerDamageTaken, targetStats.ultimateChargeRequired);
}

void TntWarsMatch::applyDamageToCore(TeamId team, float damage) {
    if (damage <= 0.0f || winningTeam_.has_value()) return; // a real, honest no-op once the match is already decided
    float& health = coreHealth_[static_cast<size_t>(team)];
    if (health <= 0.0f) return; // already destroyed -- no further real state change or double-win-check
    health = std::max(0.0f, health - damage);
    if (health <= 0.0f && matchFlow_.phase() == MatchPhase::InProgress) {
        winningTeam_ = opposingTeam(team);
        matchFlow_.advanceTo(MatchPhase::CinematicFinale);
    }
}

bool TntWarsMatch::placeTntCharge(net::PlayerId owner, glm::vec3 position) {
    if (matchFlow_.phase() != MatchPhase::InProgress) return false;
    if (!hasSelectedClass(owner) || !isAlive(owner)) return false;
    tntCharges_.push_back(tntwars::placeTntCharge(owner, position));
    return true;
}

void TntWarsMatch::tickTntCharges(float dt, const std::vector<std::pair<net::PlayerId, glm::vec3>>& playerPositions,
                                   std::vector<PlayerExplosionHit>* outPlayerHits,
                                   std::vector<DetonationEvent>* outDetonations) {
    // Kronos ("TNT Wars Foundational Playability" Phase 2): real, slowed
    // fuse timing underwater -- see Oxygen.hpp's own comment. A real
    // per-map multiplier (1.0 everywhere except IslandSea), applied to
    // `dt` itself before the real fuse countdown, not a second, parallel
    // timer -- every existing map's own tickTntCharge() call is
    // byte-for-byte unchanged (multiplier == 1.0 there).
    float fuseDt = dt * fuseDtMultiplierFor(map_);
    for (TntChargeState& charge : tntCharges_) {
        bool wasDetonated = charge.detonated;
        tickTntCharge(charge, fuseDt);
        if (wasDetonated || !charge.detonated) continue; // only real-fires damage on the real tick it detonates

        if (outDetonations != nullptr) {
            outDetonations->push_back(DetonationEvent{charge.position, charge.explosionRadius, charge.explosionMaxDamage});
        }

        for (TeamId team : {TeamId::A, TeamId::B}) {
            glm::vec3 corePos = coreWorldPosition(map_, team);
            float damage = computeExplosionDamage(charge.position, corePos, charge.explosionRadius, charge.explosionMaxDamage);
            if (damage > 0.0f) applyDamageToCore(team, damage);
        }
        applyExplosionToSegments(trenchesWall_, charge.position, charge.explosionRadius, charge.explosionMaxDamage);
        // Kronos ("Four RTX Maps" Phase 5d): the same real falloff
        // explosion damage, applied to the map's own four real "Cover_*"
        // segments -- see trenchesCover_'s own comment. Empty on every
        // non-Trenches map, same real no-op as trenchesWall_ above.
        applyExplosionToSegments(trenchesCover_, charge.position, charge.explosionRadius, charge.explosionMaxDamage);

        // Kronos ("TNT Wars Gameplay Loop"): real player damage -- see
        // PlayerExplosionHit's own header comment for why knockback
        // itself is computed by the caller, not here.
        for (const auto& [playerId, playerPos] : playerPositions) {
            float playerDamage =
                computeExplosionDamage(charge.position, playerPos, charge.explosionRadius, charge.explosionMaxDamage);
            if (playerDamage <= 0.0f) continue;
            applyDamage(charge.owner, playerId, playerDamage);
            if (outPlayerHits != nullptr) {
                outPlayerHits->push_back(PlayerExplosionHit{playerId, playerDamage, charge.position,
                                                              charge.explosionRadius, charge.explosionMaxImpulse});
            }
        }
    }
}

size_t TntWarsMatch::removeDetonatedCharges() {
    size_t before = tntCharges_.size();
    tntCharges_.erase(std::remove_if(tntCharges_.begin(), tntCharges_.end(),
                                      [](const TntChargeState& c) { return c.detonated; }),
                       tntCharges_.end());
    return before - tntCharges_.size();
}

bool TntWarsMatch::placeTntCharge(net::PlayerId owner, glm::vec3 position, ExplosiveRecipeType recipe) {
    if (matchFlow_.phase() != MatchPhase::InProgress) return false;
    if (!hasSelectedClass(owner) || !isAlive(owner)) return false;

    CraftedExplosives& crafted = playerCraftedExplosives_[owner];
    size_t index = static_cast<size_t>(recipe);
    if (crafted.counts[index] <= 0) return false;
    crafted.counts[index] -= 1;

    const ExplosiveRecipeInfo& info = explosiveRecipeInfo(recipe);
    tntCharges_.push_back(tntwars::placeTntCharge(owner, position, info.fuseSeconds, info.explosionRadius,
                                                    info.explosionMaxDamage, info.explosionMaxImpulse));
    return true;
}

bool TntWarsMatch::craft(net::PlayerId player, ExplosiveRecipeType recipe) {
    if (matchFlow_.phase() != MatchPhase::InProgress) return false;
    if (!hasSelectedClass(player) || !isAlive(player)) return false;
    return craftExplosive(playerMaterials_[player], playerCraftedExplosives_[player], recipe);
}

const CraftedExplosives& TntWarsMatch::craftedExplosives(net::PlayerId player) const {
    static const CraftedExplosives kEmpty{};
    auto it = playerCraftedExplosives_.find(player);
    return it != playerCraftedExplosives_.end() ? it->second : kEmpty;
}

bool TntWarsMatch::applyThrusterHitAt(TeamId team, size_t thrusterIndex) {
    if (matchFlow_.phase() != MatchPhase::InProgress) return false;
    if (thrusterIndex >= kThrustersPerTeam) return false;
    applyThrusterHit(skyPlatforms_[static_cast<size_t>(team)].thrusters[thrusterIndex]);
    return true;
}

bool TntWarsMatch::launchMissileAt(net::PlayerId owner, TeamId targetTeam) {
    if (matchFlow_.phase() != MatchPhase::InProgress) return false;
    if (!hasSelectedClass(owner) || !isAlive(owner)) return false;
    if (targetTeam == teamOf(owner)) return false;
    missiles_.push_back(launchMissile(owner, targetTeam));
    return true;
}

bool TntWarsMatch::interceptMissile(net::PlayerId defender, size_t missileIndex) {
    if (matchFlow_.phase() != MatchPhase::InProgress) return false;
    if (!hasSelectedClass(defender) || !isAlive(defender)) return false;
    if (missileIndex >= missiles_.size()) return false;
    if (teamOf(defender) != missiles_[missileIndex].targetTeam) return false;
    return tryInterceptMissile(missiles_[missileIndex]);
}

void TntWarsMatch::tickMissiles(float dt) {
    for (MissileState& missile : missiles_) {
        bool wasImpacted = missile.impacted;
        tickMissile(missile, dt);
        if (!wasImpacted && missile.impacted) {
            applyDamageToCore(missile.targetTeam, kMissileCoreDamage);
        }
    }
}

size_t TntWarsMatch::removeResolvedMissiles() {
    size_t before = missiles_.size();
    missiles_.erase(std::remove_if(missiles_.begin(), missiles_.end(),
                                    [](const MissileState& m) { return m.intercepted || m.impacted; }),
                     missiles_.end());
    return before - missiles_.size();
}

std::optional<glm::vec3> TntWarsMatch::triggerJumpPad(net::PlayerId player, glm::vec3 playerPosition, size_t padIndex) {
    if (matchFlow_.phase() != MatchPhase::InProgress) return std::nullopt;
    if (!hasSelectedClass(player) || !isAlive(player)) return std::nullopt;
    if (padIndex >= jumpPads_.size()) return std::nullopt;
    auto offset = tntwars::triggerJumpPad(jumpPads_[padIndex], playerPosition);
    // Kronos Milestone 12: real, direct consumer of Space's own real
    // hasZeroGravity flag -- a jump pad launches real-further under zero
    // gravity, the same real "this map's own modifiers change a real,
    // already-existing mechanic's numbers" pattern pressureFactor already
    // established for movement speed.
    if (offset.has_value() && mapModifiers().hasZeroGravity) {
        *offset *= kZeroGravityJumpMultiplier;
    }
    return offset;
}

void TntWarsMatch::tickJumpPads(float dt) {
    for (JumpPadState& pad : jumpPads_) {
        tickJumpPad(pad, dt);
    }
}

bool TntWarsMatch::applySpeedBoostTo(net::PlayerId player, float multiplier, float durationSeconds) {
    if (matchFlow_.phase() != MatchPhase::InProgress) return false;
    if (!hasSelectedClass(player) || !isAlive(player)) return false;
    applySpeedBoost(playerSpeedBoosts_[player], multiplier, durationSeconds);
    return true;
}

float TntWarsMatch::effectiveMoveSpeedFor(net::PlayerId player) const {
    float baseMoveSpeed = classTuning_.statsFor(classOf(player)).moveSpeed;
    auto it = playerSpeedBoosts_.find(player);
    float boostMultiplier = it != playerSpeedBoosts_.end() ? it->second.multiplier : 1.0f;
    return baseMoveSpeed * boostMultiplier * mapModifiers().pressureFactor;
}

bool TntWarsMatch::isTorpedoVisibleToSonar(glm::vec3 torpedoPosition) const {
    return isDetectedBySonar(torpedoPosition, sonarSourcePositions(map_), kDefaultSonarDetectionRadius);
}

void TntWarsMatch::tickSpeedBoosts(float dt) {
    for (auto& [player, boost] : playerSpeedBoosts_) {
        tickSpeedBoost(boost, dt);
    }
}

ScavengeResult TntWarsMatch::scavenge(net::PlayerId player, size_t nodeIndex, int amount) {
    ScavengeResult result;
    if (matchFlow_.phase() != MatchPhase::InProgress) return result;
    if (!hasSelectedClass(player) || !isAlive(player)) return result;
    if (nodeIndex >= scavengeNodes_.size()) return result;

    result = scavengeNode(scavengeNodes_[nodeIndex], teamOf(player), amount);
    if (result.success) {
        addScavengedMaterial(playerMaterials_[player], result.material, result.quantityCollected);
    }
    return result;
}

const ScavengedMaterials& TntWarsMatch::scavengedMaterials(net::PlayerId player) const {
    static const ScavengedMaterials kEmpty{};
    auto it = playerMaterials_.find(player);
    return it != playerMaterials_.end() ? it->second : kEmpty;
}

UpgradeResult TntWarsMatch::purchaseUpgrade(net::PlayerId player, UpgradeCategory category) {
    UpgradeResult result;
    if (matchFlow_.phase() != MatchPhase::InProgress) return result;
    if (!hasSelectedClass(player) || !isAlive(player)) return result;
    return tntwars::purchaseUpgrade(playerUpgrades_[player], playerMaterials_[player], category);
}

const PlayerUpgrades& TntWarsMatch::playerUpgrades(net::PlayerId player) const {
    static const PlayerUpgrades kEmpty{};
    auto it = playerUpgrades_.find(player);
    return it != playerUpgrades_.end() ? it->second : kEmpty;
}

void TntWarsMatch::tickScavengeNodes(float dt) {
    for (ScavengeNodeState& node : scavengeNodes_) {
        tickScavengeNodeRespawn(node, dt);
    }
}

TntWarsMatch::FireResult TntWarsMatch::fireWeapon(net::PlayerId player, glm::vec3 origin, glm::vec3 aimDirection,
                                                    float nowSeconds) {
    FireResult result;
    if (!hasSelectedClass(player) || !isAlive(player)) return result;

    PlayerClassType classType = classOf(player);
    ClassStats stats = classTuning_.statsFor(classType);
    float maxPerSecond = stats.primaryCooldownSeconds > 0.0f ? 1.0f / stats.primaryCooldownSeconds : 1.0f;
    if (!antiCheat_.checkFireRate(player, maxPerSecond, nowSeconds)) {
        antiCheat_.recordSuspiciousRequest(player, "TntWarsFireRate", nowSeconds);
        return result;
    }

    result.accepted = true;
    result.projectile = spawnProjectile(primaryProjectileForClass(classType), player, origin, aimDirection, stats.primaryDamage);
    return result;
}

TntWarsMatch::UltimateResult TntWarsMatch::triggerUltimate(net::PlayerId player, float nowSeconds) {
    UltimateResult result;
    if (!hasSelectedClass(player) || !isAlive(player)) return result;

    PlayerClassType classType = classOf(player);
    ClassStats stats = classTuning_.statsFor(classType);
    if (!ultimateCharge_.isReady(player, stats.ultimateChargeRequired)) {
        antiCheat_.recordSuspiciousRequest(player, "TntWarsUltimateRequest", nowSeconds);
        return result;
    }

    ultimateCharge_.consume(player);
    result.accepted = true;
    result.type = ultimateForClass(classType);
    return result;
}

} // namespace engine::tntwars
