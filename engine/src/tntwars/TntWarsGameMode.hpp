#pragma once

#include "core/Audio.hpp"
#include "core/GameMode.hpp"
#include "core/ProceduralMaterials.hpp"
#include "core/Terrain.hpp"
#include "net/NetworkSession.hpp"
#include "tntwars/DestructibleGeometryVisual.hpp"
#include "tntwars/ScavengeNodeVisual.hpp"
#include "tntwars/TraversalChallenge.hpp"
#include "tntwars/TntWarsUpgradeStation.hpp"
#include "tntwars/AtmosphereZones.hpp"
#include "tntwars/FlickerLight.hpp"
#include "tntwars/SpaceTraversal.hpp"
#include "tntwars/CombatMob.hpp"
#include "tntwars/PvPNode.hpp"
#include "tntwars/CombatFx.hpp"
#include "tntwars/Explosives.hpp"
#include "tntwars/Decal.hpp"
#include "tntwars/Respawn.hpp"
#include "tntwars/TntChargeVisual.hpp"
#include "tntwars/ProjectileVisual.hpp"

#include <deque>
#include <string>
#include <vector>

namespace engine::tntwars {

// TNT Wars: input, match sync, explosives, traversal, HUD and chat for the live game.
class TntWarsGameMode final : public core::GameMode {
public:
    [[nodiscard]] std::vector<tntwars::AtmosphereZone>& tntWarsAtmosphereZones() { return tntWarsAtmosphereZones_; }
    void setLiveMode(core::Audio& audio, bool enabled, net::PlayerId localPlayer);
    [[nodiscard]] std::vector<tntwars::DestructibleSegmentVisual>& tntWarsWallVisuals() { return tntWarsWallVisuals_; }
    [[nodiscard]] std::vector<tntwars::DestructibleSegmentVisual>& tntWarsCoverVisuals() { return tntWarsCoverVisuals_; }
    [[nodiscard]] std::vector<tntwars::JumpPadState>& tntWarsJumpPads() { return tntWarsJumpPads_; }
    [[nodiscard]] std::vector<tntwars::ZipLineState>& tntWarsZipLines() { return tntWarsZipLines_; }
    [[nodiscard]] std::vector<tntwars::BoosterPadState>& tntWarsBoosterPads() { return tntWarsBoosterPads_; }
    [[nodiscard]] std::vector<tntwars::ZeroGravityZone>& tntWarsZeroGravityZones() { return tntWarsZeroGravityZones_; }
    [[nodiscard]] std::vector<tntwars::GravityWellState>& tntWarsGravityWells() { return tntWarsGravityWells_; }
    [[nodiscard]] std::vector<tntwars::CombatMobInstance>& tntWarsCombatMobs() { return tntWarsCombatMobs_; }
    [[nodiscard]] std::vector<tntwars::PvPNodeState>& tntWarsPvPNodes() { return tntWarsPvPNodes_; }
    [[nodiscard]] std::vector<core::EntityId>& tntWarsPvPNodeVisuals() { return tntWarsPvPNodeVisuals_; }
    [[nodiscard]] std::vector<tntwars::GrenadeState>& tntWarsGrenades() { return tntWarsGrenades_; }
    [[nodiscard]] std::vector<tntwars::ExplosiveBarrelState>& tntWarsExplosiveBarrels() {
        return tntWarsExplosiveBarrels_;
    }
    void setTntWarsMaterials(const core::ProceduralMaterialLibrary& materials) { tntWarsMaterials_ = materials; }
    void setTntWarsRespawnPosition(glm::vec3 position) { tntWarsRespawnPosition_ = position; }
    [[nodiscard]] std::vector<tntwars::ScavengeNodeVisual>& tntWarsScavengeNodeVisuals() {
        return tntWarsScavengeNodeVisuals_;
    }
    [[nodiscard]] std::vector<tntwars::TraversalChallengeState>& tntWarsTraversalChallenges() {
        return tntWarsTraversalChallenges_;
    }
    [[nodiscard]] std::vector<tntwars::DestructibleSegment>& tntWarsExtraDestructibles() {
        return tntWarsExtraDestructibles_;
    }
    [[nodiscard]] std::vector<tntwars::DestructibleSegmentVisual>& tntWarsExtraDestructibleVisuals() {
        return tntWarsExtraDestructibleVisuals_;
    }
    void setTntWarsCollapsibleIslands(std::vector<tntwars::DestructibleSegment> segments,
                                       std::vector<core::Terrain*> terrains) {
        tntWarsCollapsibleIslandSegments_ = std::move(segments);
        tntWarsCollapsibleIslandTerrains_ = std::move(terrains);
        tntWarsCollapsibleIslandAlreadyCollapsed_.assign(tntWarsCollapsibleIslandSegments_.size(), false);
    }

    void onRawEvent(core::GameModeContext& ctx, const SDL_Event& event) override;
    void tickEnvironment(core::GameModeContext& ctx, float dt) override;
    void adjustLighting(core::GameModeContext& ctx, core::SceneLighting& lighting, const glm::vec3& playerPos) override;
    void tickWorld(core::GameModeContext& ctx, float dt, core::EntityId character, const glm::vec3& characterPos) override;

private:
    std::vector<tntwars::AtmosphereZone> tntWarsAtmosphereZones_;
    bool tntWarsLiveModeEnabled_ = false;
    net::PlayerId tntWarsLocalPlayerId_ = 0;
    std::vector<tntwars::DestructibleSegmentVisual> tntWarsWallVisuals_;
    std::vector<tntwars::DestructibleSegmentVisual> tntWarsCoverVisuals_;
    std::vector<tntwars::JumpPadState> tntWarsJumpPads_;
    std::vector<tntwars::ZipLineState> tntWarsZipLines_;
    std::vector<tntwars::BoosterPadState> tntWarsBoosterPads_;
    std::vector<tntwars::ZeroGravityZone> tntWarsZeroGravityZones_;
    std::vector<tntwars::GravityWellState> tntWarsGravityWells_;
    std::vector<tntwars::CombatMobInstance> tntWarsCombatMobs_;
    std::vector<tntwars::PvPNodeState> tntWarsPvPNodes_;
    std::vector<core::EntityId> tntWarsPvPNodeVisuals_;
    tntwars::GameplayShakeState tntWarsShakeState_;
    core::SoundHandle tntWarsExplosionSound_ = core::kInvalidSoundHandle;
    core::SoundHandle tntWarsGrenadeSound_ = core::kInvalidSoundHandle;
    std::vector<tntwars::GrenadeState> tntWarsGrenades_;
    bool tntWarsGrenadeThrowKeyWasDown_ = false;
    std::vector<tntwars::ExplosiveBarrelState> tntWarsExplosiveBarrels_;
    std::vector<tntwars::DecalState> tntWarsDecals_;
    core::ProceduralMaterialLibrary tntWarsMaterials_;
    bool tntWarsCraftKeyWasDown_ = false;
    bool tntWarsPlaceCraftedKeyWasDown_ = false;
    std::vector<core::EntityId> tntWarsChargeVisuals_;
    std::vector<tntwars::ProjectileVisualState> tntWarsProjectileVisuals_;
    std::vector<std::pair<core::EntityId, float>> tntWarsBurstEntities_;
    bool tntWarsSettingsOverlayVisible_ = false;
    bool tntWarsSettingsKeyWasDown_ = false;
    bool tntWarsLeaderboardOverlayVisible_ = false;
    bool tntWarsLeaderboardKeyWasDown_ = false;
    bool tntWarsChatFocused_ = false;
    std::string tntWarsChatInputBuffer_;
    bool tntWarsChatCallbackRegistered_ = false;
    struct TntWarsChatEntry {
        std::string senderName;
        std::string body;
    };
    std::deque<TntWarsChatEntry> tntWarsChatHistory_;
    static constexpr size_t kTntWarsChatHistoryLimit = 50;
    static constexpr size_t kTntWarsChatVisibleLines = 8;
    glm::vec3 tntWarsRespawnPosition_{0.0f};
    tntwars::RespawnState tntWarsRespawnState_;
    float tntWarsMatchElapsedSeconds_ = 0.0f;
    std::vector<tntwars::ZipLineArcLengthTable> tntWarsZipLineArcTables_;
    int tntWarsActiveZipLineIndex_ = -1;
    tntwars::ZipLineRiderState tntWarsZipLineRider_;
    int tntWarsZipLineHintIndex_ = -1;
    bool tntWarsZipLineMountKeyWasDown_ = false;
    bool tntWarsZipLineDismountKeyWasDown_ = false;
    std::vector<tntwars::ScavengeNodeVisual> tntWarsScavengeNodeVisuals_;
    int tntWarsScavengeHintIndex_ = -1;
    bool tntWarsScavengeKeyWasDown_ = false;
    std::vector<tntwars::TraversalChallengeState> tntWarsTraversalChallenges_;
    core::EntityId tntWarsUpgradeHintEntity_ = core::kNullEntity;
    bool tntWarsUpgradeKeyWasDown_ = false;
    bool tntWarsSuitBaseSpeedCaptured_ = false;
    float tntWarsSuitBaseWalkSpeed_ = 0.0f;
    float tntWarsSuitBaseRunSpeed_ = 0.0f;
    std::vector<tntwars::DestructibleSegment> tntWarsExtraDestructibles_;
    std::vector<tntwars::DestructibleSegmentVisual> tntWarsExtraDestructibleVisuals_;
    std::vector<tntwars::DestructibleSegment> tntWarsCollapsibleIslandSegments_;
    std::vector<core::Terrain*> tntWarsCollapsibleIslandTerrains_;
    std::vector<bool> tntWarsCollapsibleIslandAlreadyCollapsed_;
    bool tntWarsPlaceTntKeyWasDown_ = false;
    bool tntWarsFireKeyWasDown_ = false;
    bool tntWarsUltimateKeyWasDown_ = false;
    bool tntWarsClassKeyWasDown_[5] = {false, false, false, false, false};
};

} // namespace engine::tntwars
