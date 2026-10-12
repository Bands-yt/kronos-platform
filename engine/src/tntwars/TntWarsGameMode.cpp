#include "core/RobloxServices.hpp"
#include "tntwars/TntWarsNetRules.hpp"
#include "core/Hierarchy.hpp"
#include <cstdlib>
#include "core/PlayerAvatar.hpp"
#include "core/RenderResourceLoaders.hpp"
#include "core/ScriptChatApi.hpp"
#include "core/Utf8.hpp"
#include "net/HttpWorkerPool.hpp"
#include "net/NetworkedMovement.hpp"
#include "safety/GeminiModerationClient.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <SDL2/SDL.h>
#include "core/Economy.hpp"
#include "core/EmoteSystem.hpp"
#include "core/InstanceSignals.hpp"
#include "core/InstanceTree.hpp"
#include "core/Inventory.hpp"
#include "core/Logger.hpp"
#include "core/OreNode.hpp"
#include "core/PropAnimation.hpp"
#include "core/RobloxPlayers.hpp"
#include "core/RobloxRemoteNet.hpp"
#include "core/RobloxReplication.hpp"
#include "core/PartBodies.hpp"
#include "core/ScriptHotReload.hpp"
#include "core/Shop.hpp"
#include "core/DeterministicMath.hpp"
#include "core/Terrain.hpp"
#include "core/UpgradeSystem.hpp"
#include "core/VisualFeedback.hpp"
#include "core/WorldProp.hpp"
#include "runtime/GameLoop.hpp"
#include <memory>
#include <random>
#include <deque>
#include "core/ResourceManager.hpp"
#include "core/WorldStreaming.hpp"
#include "core/Audio.hpp"
#include "core/AnimationDatabase.hpp"
#include "core/AvatarController.hpp"
#include "core/AvatarHair.hpp"
#include "core/AvatarLoadout.hpp"
#include "core/Camera.hpp"
#include "core/GameMode.hpp"
#include "core/CatalogueIndex.hpp"
#include "core/CharacterController.hpp"
#include "core/ECS.hpp"
#include "core/Interactable.hpp"
#include "core/Mesh.hpp"
#include "core/NativePluginManager.hpp"
#include "plugin/PluginHost.hpp"
#include "core/Navigation.hpp"
#include "core/ParticleSystem.hpp"
#include "core/PerformanceDiagnostics.hpp"
#include "core/Physics.hpp"
#include "core/ProcessStats.hpp"
#include "core/Profiler.hpp"
#include "core/ResourcePaths.hpp"
#include "core/RiggedAvatar.hpp"
#include "core/RiggedMesh.hpp"
#include "net/NetworkSession.hpp"
#include "core/RollbackScript.hpp"
#include "core/PythonScripting.hpp"
#include "net/RollbackNetSession.hpp"
#include "core/RuntimeAnimationPlayer.hpp"
#include "core/ScriptAvatarApi.hpp"
#include "core/ScriptNetworkApi.hpp"
#include "core/ScriptUiApi.hpp"
#include "core/ScriptAudioApi.hpp"
#include "core/ScriptUiLayoutApi.hpp"
#include "core/ScriptWorldApi.hpp"
#include "core/TrailerScriptApi.hpp"
#include "trailer/RenderShowcase.hpp"
#include "core/ProceduralMaterials.hpp"
#include "core/UIRenderer.hpp"
#include "core/Texture.hpp"
#include "core/TimeOfDay.hpp"
#include "core/Renderer.hpp"
#include "core/Scripting.hpp"
#include "core/Window.hpp"
#include "core/Wind.hpp"
#include "platform_adapters/UnifiedInput.hpp"
#include "tntwars/TntWarsGameMode.hpp"

namespace engine::tntwars {

using namespace core;

void TntWarsGameMode::setLiveMode(Audio& audio, bool enabled, net::PlayerId localPlayer) {
    tntWarsLiveModeEnabled_ = enabled;
    tntWarsLocalPlayerId_ = localPlayer;
    if (enabled && tntWarsExplosionSound_ == kInvalidSoundHandle) {
        std::string assetDir = resolveResourceDir(executableDirectory(), "assets", ENGINE_ASSET_DIR);
        tntWarsExplosionSound_ = audio.loadSound(assetDir + "/audio/tnt_explosion.wav");
        tntWarsGrenadeSound_ = audio.loadSound(assetDir + "/audio/grenade_explosion.wav");
    }
}

void TntWarsGameMode::onRawEvent(GameModeContext& ctx, const SDL_Event& event) {
    auto& network = ctx.network;
    if (!tntWarsLiveModeEnabled_) return;

    if (!tntWarsChatFocused_) {
        if (event.type == SDL_KEYDOWN && event.key.repeat == 0 &&
            (event.key.keysym.sym == SDLK_SLASH || event.key.keysym.sym == SDLK_RETURN ||
             event.key.keysym.sym == SDLK_KP_ENTER)) {
            tntWarsChatFocused_ = true;
            tntWarsChatInputBuffer_.clear();
            SDL_StartTextInput();
        }
        return;
    }

    if (event.type == SDL_TEXTINPUT) {
        if (tntWarsChatInputBuffer_.size() + std::strlen(event.text.text) <= net::ChatMessagePacket::kMaxBodyBytes) {
            tntWarsChatInputBuffer_ += event.text.text;
        }
    } else if (event.type == SDL_KEYDOWN) {
        if (event.key.keysym.sym == SDLK_BACKSPACE && !tntWarsChatInputBuffer_.empty()) {
            tntWarsChatInputBuffer_ = utf8TrimTrailingCharacter(tntWarsChatInputBuffer_);
        } else if (event.key.keysym.sym == SDLK_RETURN || event.key.keysym.sym == SDLK_KP_ENTER) {
            if (!tntWarsChatInputBuffer_.empty()) network.sendChatMessage(tntWarsChatInputBuffer_);
            tntWarsChatInputBuffer_.clear();
            tntWarsChatFocused_ = false;
            SDL_StopTextInput();
        } else if (event.key.keysym.sym == SDLK_ESCAPE) {
            tntWarsChatInputBuffer_.clear();
            tntWarsChatFocused_ = false;
            SDL_StopTextInput();
        }
    }}

void TntWarsGameMode::tickEnvironment(GameModeContext& ctx, float dt) {
    (void)dt;
    tickFlickerLights(ctx.ecs, ctx.totalSimTime);
}

void TntWarsGameMode::adjustLighting(GameModeContext& ctx, SceneLighting& lighting, const glm::vec3& playerPos) {
    if (tntWarsAtmosphereZones_.empty()) return;
    auto& renderer = ctx.renderer;
    tntwars::AtmosphereSample baseline;
    baseline.fogColor = lighting.fogColor;
    baseline.fogDensity = lighting.fogDensity;
    baseline.exposure = renderer.isCinematicModeEnabled() ? 1.0f : renderer.exposure();
    tntwars::AtmosphereSample sample = tntwars::sampleAtmosphereZones(tntWarsAtmosphereZones_, playerPos, baseline);
    lighting.fogColor = sample.fogColor;
    lighting.fogDensity = renderer.isVolumetricFogEnabled() ? sample.fogDensity : 0.0f;
    renderer.setExposure(sample.exposure);}

void TntWarsGameMode::tickWorld(GameModeContext& ctx, float dt, EntityId character, const glm::vec3& characterPos) {
    if (!tntWarsLiveModeEnabled_) return;
    ECS& ecs = ctx.ecs;
    Physics& physics = ctx.physics;
    CharacterController& controller = ctx.controller;
    Camera& camera = ctx.camera;
    UIRenderer& ui = ctx.ui;
    Renderer& renderer = ctx.renderer;
    auto& input = ctx.input;
    auto& network = ctx.network;
    auto& meshes = ctx.meshes;
    auto& audio = ctx.audio;
    tntwars::TntWarsMatch& match = engine::tntwars::TntWarsNetRules::of(network).match();
    match.matchFlow().tick(dt);

    if (!tntWarsChatCallbackRegistered_) {
        tntWarsChatCallbackRegistered_ = true;
        network.setOnChatMessageReceived([this, &network](net::PlayerId sender, const std::string& text) {
            std::string senderName = "Player";
            const auto& known = network.clientKnownPlayers();
            auto it = known.find(sender);
            if (it != known.end()) senderName = it->second;
            tntWarsChatHistory_.push_back({senderName, text});
            while (tntWarsChatHistory_.size() > kTntWarsChatHistoryLimit) tntWarsChatHistory_.pop_front();
        });
    }

    constexpr net::PlayerId kPveMobDealerId = 0; // real, reserved, never-registered PlayerId -- see tntWarsCombatMobs()'s own comment
    auto detonateEnvironment = [&](glm::vec3 position, float radius, float maxDamage, core::SoundHandle sound) {
        tntwars::applyExplosionToSegments(tntWarsExtraDestructibles_, position, radius, maxDamage);
        tntwars::applyExplosionToSegments(tntWarsCollapsibleIslandSegments_, position, radius, maxDamage);
        for (tntwars::ExplosiveBarrelState& barrel : tntWarsExplosiveBarrels_) {
            if (barrel.hasExploded) continue;
            float dist = glm::length(barrel.segment.position - position);
            if (dist >= radius) continue;
            tntwars::applyDamageToSegment(barrel.segment, maxDamage * (1.0f - dist / radius));
        }
        for (tntwars::CombatMobInstance& mob : tntWarsCombatMobs_) {
            if (mob.defeated) continue;
            float dist = glm::length(mob.position - position);
            if (dist >= radius) continue;
            tntwars::applyDamageToCombatMob(mob, ecs, maxDamage * (1.0f - dist / radius));
            if (mob.torso != kNullEntity) tntwars::triggerHitMarkerFlash(ecs, mob.torso);
        }
        tntWarsBurstEntities_.emplace_back(tntwars::spawnExplosionParticleBurst(ecs, position), 1.2f);
        if (sound != kInvalidSoundHandle) audio.playOneShot(sound);
        if (character != kNullEntity) {
            tntwars::addShakeTrauma(tntWarsShakeState_, tntwars::explosionShakeTrauma(position, characterPos, radius * 2.5f));
        }

        constexpr float kDecalFloorSearchDistance = 6.0f;
        constexpr size_t kMaxLiveDecals = 32;
        Physics::RaycastHit decalHit = physics.raycast(position, glm::vec3(0.0f, -1.0f, 0.0f), kDecalFloorSearchDistance);
        if (decalHit.hit) {
            if (tntWarsDecals_.size() >= kMaxLiveDecals) {
                ecs.destroyEntity(tntWarsDecals_.front().entity);
                tntWarsDecals_.erase(tntWarsDecals_.begin());
            }
            tntWarsDecals_.push_back(tntwars::spawnScorchDecal(
                ecs, meshes, tntWarsMaterials_, renderer.allocator(), renderer.device(),
                renderer.commandPool(), renderer.graphicsQueue(), decalHit.point, decalHit.normal,
                radius * 0.5f));
        }
    };
    auto damagePlayerFrom = [&](glm::vec3 position, float radius, float maxDamage, float maxImpulse,
                                 net::PlayerId dealer) {
        if (character == kNullEntity) return;
        float dist = glm::length(characterPos - position);
        if (dist >= radius) return;
        float damage = maxDamage * (1.0f - dist / radius);
        match.applyDamage(dealer, tntWarsLocalPlayerId_, damage);
        tntwars::ExplosionImpulse impulse = tntwars::computeExplosionImpulse(position, characterPos, radius, maxImpulse);
        physics.applyImpulse(character, ecs, impulse.direction * impulse.magnitude);
        tntwars::triggerHitMarkerFlash(ecs, character);
        std::fprintf(stdout, "TNT Wars: explosion hit for %.1f damage (%.1f HP remaining)\n", damage,
                     match.health(tntWarsLocalPlayerId_));
    };

    std::vector<std::pair<net::PlayerId, glm::vec3>> tntWarsPlayerPositions;
    if (character != kNullEntity) tntWarsPlayerPositions.push_back({tntWarsLocalPlayerId_, characterPos});
    std::vector<tntwars::TntWarsMatch::PlayerExplosionHit> tntWarsPlayerHits;
    std::vector<tntwars::TntWarsMatch::DetonationEvent> tntWarsDetonations;
    match.tickTntCharges(dt, tntWarsPlayerPositions, &tntWarsPlayerHits, &tntWarsDetonations);

    {
        const std::vector<tntwars::TntChargeState>& liveCharges = match.tntCharges();
        while (tntWarsChargeVisuals_.size() < liveCharges.size()) {
            const tntwars::TntChargeState& charge = liveCharges[tntWarsChargeVisuals_.size()];
            std::string name = "TntWars_Charge_" + std::to_string(tntWarsChargeVisuals_.size());
            tntWarsChargeVisuals_.push_back(tntwars::spawnTntChargeVisual(
                ecs, meshes, tntWarsMaterials_, renderer.allocator(), renderer.device(),
                renderer.commandPool(), renderer.graphicsQueue(), charge.position, name.c_str()));
        }
        for (size_t i = 0; i < liveCharges.size() && i < tntWarsChargeVisuals_.size(); ++i) {
            if (!liveCharges[i].detonated || tntWarsChargeVisuals_[i] == kNullEntity) continue;
            ecs.destroyEntity(tntWarsChargeVisuals_[i]);
            tntWarsChargeVisuals_[i] = kNullEntity;
        }
    }

    for (const auto& hit : tntWarsPlayerHits) {
        if (hit.player != tntWarsLocalPlayerId_ || character == kNullEntity) continue;
        tntwars::ExplosionImpulse impulse = tntwars::computeExplosionImpulse(
            hit.explosionCenter, characterPos, hit.explosionRadius, hit.explosionMaxImpulse);
        physics.applyImpulse(character, ecs, impulse.direction * impulse.magnitude);
        tntwars::triggerHitMarkerFlash(ecs, character);
        tntwars::addShakeTrauma(tntWarsShakeState_,
                                 tntwars::explosionShakeTrauma(hit.explosionCenter, characterPos, hit.explosionRadius * 2.5f));
        std::fprintf(stdout, "TNT Wars: explosion hit for %.1f damage (%.1f HP remaining)\n", hit.damage,
                     match.health(tntWarsLocalPlayerId_));
    }

    for (const auto& detonation : tntWarsDetonations) {
        detonateEnvironment(detonation.position, detonation.explosionRadius, detonation.explosionMaxDamage,
                             tntWarsExplosionSound_);
    }

    for (tntwars::GrenadeState& grenade : tntWarsGrenades_) {
        tntwars::tickGrenadeTrajectory(grenade, physics.gravity(), dt);
        tntwars::tickGrenadeFuse(grenade, dt);
        if (grenade.detonated) {
            detonateEnvironment(grenade.position, grenade.explosionRadius, grenade.explosionMaxDamage,
                                 tntWarsGrenadeSound_);
            damagePlayerFrom(grenade.position, grenade.explosionRadius, grenade.explosionMaxDamage,
                              grenade.explosionMaxImpulse, grenade.owner);
        }
    }
    tntWarsGrenades_.erase(std::remove_if(tntWarsGrenades_.begin(), tntWarsGrenades_.end(),
                                           [](const tntwars::GrenadeState& g) { return g.detonated; }),
                            tntWarsGrenades_.end());

    for (tntwars::ExplosiveBarrelState& barrel : tntWarsExplosiveBarrels_) {
        if (tntwars::tickExplosiveBarrelDetonation(barrel)) {
            detonateEnvironment(barrel.segment.position, barrel.explosionRadius, barrel.explosionMaxDamage,
                                 tntWarsExplosionSound_);
            damagePlayerFrom(barrel.segment.position, barrel.explosionRadius, barrel.explosionMaxDamage,
                              barrel.explosionMaxImpulse, kPveMobDealerId);
            tntwars::hideExplosiveBarrelVisual(barrel, ecs, physics);
            std::fprintf(stdout, "TNT Wars: explosive barrel detonated!\n");
        }
    }

    for (tntwars::ProjectileVisualState& pv : tntWarsProjectileVisuals_) {
        tntwars::stepProjectile(pv.projectile, dt);
        if (pv.projectile.expired) continue;
        for (tntwars::CombatMobInstance& mob : tntWarsCombatMobs_) {
            if (mob.defeated) continue;
            if (!tntwars::projectileHitsTarget(pv.projectile, mob.position, 1.0f)) continue;
            tntwars::applyDamageToCombatMob(mob, ecs, pv.projectile.damage);
            if (mob.torso != kNullEntity) tntwars::triggerHitMarkerFlash(ecs, mob.torso);
            tntWarsBurstEntities_.emplace_back(tntwars::spawnProjectileImpactBurst(ecs, pv.projectile.position),
                                                0.6f);
            std::fprintf(stdout, "TNT Wars: shot hit for %.1f damage\n", pv.projectile.damage);
            pv.projectile.expired = true;
            break;
        }
        if (!pv.projectile.expired) tntwars::updateProjectileVisualTransform(pv.entity, ecs, pv.projectile);
    }
    for (tntwars::ProjectileVisualState& pv : tntWarsProjectileVisuals_) {
        if (pv.projectile.expired) ecs.destroyEntity(pv.entity);
    }
    tntWarsProjectileVisuals_.erase(
        std::remove_if(tntWarsProjectileVisuals_.begin(), tntWarsProjectileVisuals_.end(),
                        [](const tntwars::ProjectileVisualState& pv) { return pv.projectile.expired; }),
        tntWarsProjectileVisuals_.end());

    tntwars::tickGameplayShake(tntWarsShakeState_, dt);
    if (!ctx.reducedMotion) camera.position += tntwars::sampleGameplayShakeOffset(tntWarsShakeState_);

    for (size_t i = tntWarsDecals_.size(); i-- > 0;) {
        if (tntwars::tickDecalExpiry(tntWarsDecals_[i], dt)) {
            ecs.destroyEntity(tntWarsDecals_[i].entity);
            tntWarsDecals_.erase(tntWarsDecals_.begin() + static_cast<long>(i));
        }
    }
    for (size_t i = tntWarsBurstEntities_.size(); i-- > 0;) {
        tntWarsBurstEntities_[i].second -= dt;
        if (tntWarsBurstEntities_[i].second <= 0.0f) {
            ecs.destroyEntity(tntWarsBurstEntities_[i].first);
            tntWarsBurstEntities_.erase(tntWarsBurstEntities_.begin() + static_cast<long>(i));
        }
    }
    tntwars::tickDestructibleWallVisual(tntWarsExtraDestructibles_, tntWarsExtraDestructibleVisuals_, ecs,
                                         physics, dt);
    for (size_t i = 0; i < tntWarsCollapsibleIslandSegments_.size() && i < tntWarsCollapsibleIslandTerrains_.size();
         ++i) {
        if (i >= tntWarsCollapsibleIslandAlreadyCollapsed_.size() || tntWarsCollapsibleIslandAlreadyCollapsed_[i]) {
            continue;
        }
        if (!tntwars::isSegmentDestroyed(tntWarsCollapsibleIslandSegments_[i])) continue;
        Terrain* collapsingTerrain = tntWarsCollapsibleIslandTerrains_[i];
        if (collapsingTerrain == nullptr) continue;
        const Terrain::CreateInfo& info = collapsingTerrain->info();
        std::vector<float> voidHeights(static_cast<size_t>(info.gridResolution) * info.gridResolution,
                                        info.origin.y - 100.0f);
        collapsingTerrain->restoreHeightSnapshot(voidHeights);
        tntWarsCollapsibleIslandAlreadyCollapsed_[i] = true;
        std::fprintf(stdout, "TNT Wars: minor island %zu real-collapsed.\n", i);
    }

    if (character != kNullEntity) {
        float tntWarsTraversalMultiplier =
            tntwars::traversalSpeedMultiplier(match.playerUpgrades(tntWarsLocalPlayerId_).traversalTier);
        for (tntwars::JumpPadState& pad : tntWarsJumpPads_) {
            tntwars::tickJumpPad(pad, dt);
            if (auto launch = tntwars::triggerJumpPad(pad, characterPos)) {
                float boostedLaunch = launch->y * tntWarsTraversalMultiplier;
                physics.setVerticalVelocity(character, ecs, boostedLaunch);
                std::fprintf(stdout, "TNT Wars: jump pad launch (%.1f)\n", boostedLaunch);
            }
        }

        for (tntwars::BoosterPadState& pad : tntWarsBoosterPads_) {
            tntwars::tickBoosterPad(pad, dt);
            if (auto launch = tntwars::triggerBoosterPad(pad, characterPos)) {
                glm::vec3 boostedLaunch = *launch * tntWarsTraversalMultiplier;
                physics.setHorizontalVelocity(character, ecs, glm::vec2(boostedLaunch.x, boostedLaunch.z));
                physics.setVerticalVelocity(character, ecs, boostedLaunch.y);
                std::fprintf(stdout, "TNT Wars: booster pad launch (%.1f,%.1f,%.1f)\n", boostedLaunch.x,
                             boostedLaunch.y, boostedLaunch.z);
            }
        }

        for (const tntwars::ZeroGravityZone& zone : tntWarsZeroGravityZones_) {
            glm::vec3 compensation = tntwars::zeroGravityCompensation(zone, characterPos, physics.gravity(), dt);
            if (compensation != glm::vec3(0.0f)) physics.applyImpulse(character, ecs, compensation);
        }
        for (const tntwars::GravityWellState& well : tntWarsGravityWells_) {
            glm::vec3 pull = tntwars::gravityWellPull(well, characterPos, dt);
            if (pull != glm::vec3(0.0f)) physics.applyImpulse(character, ecs, pull);
        }

        constexpr net::PlayerId kPveMobDealerId = 0;
        for (tntwars::CombatMobInstance& mob : tntWarsCombatMobs_) {
            float damage = tntwars::tickCombatMob(mob, ecs, characterPos, dt);
            if (damage > 0.0f) {
                match.applyDamage(kPveMobDealerId, tntWarsLocalPlayerId_, damage);
                std::fprintf(stdout, "TNT Wars: mob attack for %.1f damage (%.1f HP remaining)\n", damage,
                             match.health(tntWarsLocalPlayerId_));
            }
        }

        for (size_t i = 0; i < tntWarsPvPNodes_.size(); ++i) {
            tntwars::PvPNodeState& node = tntWarsPvPNodes_[i];
            bool inRange = glm::length(characterPos - node.position) <= node.radius;
            tntwars::TeamId localTeam = match.teamOf(tntWarsLocalPlayerId_);
            bool teamAPresent = inRange && localTeam == tntwars::TeamId::A;
            bool teamBPresent = inRange && localTeam == tntwars::TeamId::B;
            bool wasControlled = node.controllingTeam.has_value();
            tntwars::tickPvPNodeCapture(node, teamAPresent, teamBPresent, dt);
            if (!wasControlled && node.controllingTeam.has_value()) {
                std::fprintf(stdout, "TNT Wars: PvP node %zu captured by Team %s!\n", i,
                             *node.controllingTeam == tntwars::TeamId::A ? "A" : "B");
            }
            if (i < tntWarsPvPNodeVisuals_.size()) {
                tntwars::tickPvPNodeVisual(node, tntWarsPvPNodeVisuals_[i], ecs);
            }
        }

        if (tntWarsZipLineArcTables_.size() != tntWarsZipLines_.size()) {
            tntWarsZipLineArcTables_.clear();
            tntWarsZipLineArcTables_.reserve(tntWarsZipLines_.size());
            for (const tntwars::ZipLineState& zipLine : tntWarsZipLines_) {
                tntWarsZipLineArcTables_.push_back(tntwars::buildZipLineArcLengthTable(zipLine));
            }
            tntWarsActiveZipLineIndex_ = -1; // real, honest reset -- the old index may no longer be valid
        }

        if (tntWarsActiveZipLineIndex_ < 0) {
            int nearestIndex = -1;
            float nearestT = 0.0f;
            float nearestDist = std::numeric_limits<float>::max();
            for (size_t i = 0; i < tntWarsZipLines_.size(); ++i) {
                const tntwars::ZipLineState& zipLine = tntWarsZipLines_[i];
                float t = 0.0f;
                float dist = tntwars::distanceToZipLineCurve(zipLine, characterPos, &t);
                if (dist <= zipLine.triggerRadius && dist < nearestDist) {
                    nearestDist = dist;
                    nearestIndex = static_cast<int>(i);
                    nearestT = t;
                }
            }

            if (nearestIndex != tntWarsZipLineHintIndex_) {
                std::fprintf(stdout, "[UI hint] %s\n", nearestIndex >= 0 ? "Press E to zip-line" : "(none)");
                tntWarsZipLineHintIndex_ = nearestIndex;
            }

            bool mountKeyDown = input.isActionDown("Interact");
            if (nearestIndex >= 0 && mountKeyDown && !tntWarsZipLineMountKeyWasDown_) {
                const tntwars::ZipLineState& zipLine = tntWarsZipLines_[nearestIndex];
                float distToStart = glm::length(characterPos - zipLine.start);
                float distToEnd = glm::length(characterPos - zipLine.end);
                tntWarsZipLineRider_.direction = (distToStart <= distToEnd) ? 1.0f : -1.0f;
                tntWarsZipLineRider_.distanceTraveled =
                    tntwars::zipLineTToDistance(tntWarsZipLineArcTables_[nearestIndex], nearestT);
                tntWarsActiveZipLineIndex_ = nearestIndex;
            }
            tntWarsZipLineMountKeyWasDown_ = mountKeyDown;
        }

        if (tntWarsActiveZipLineIndex_ >= 0) {
            bool jumpDown = input.isActionDown("Jump");
            if (jumpDown && !tntWarsZipLineDismountKeyWasDown_) {
                tntWarsActiveZipLineIndex_ = -1;
            }
            tntWarsZipLineDismountKeyWasDown_ = jumpDown;
        }

        if (tntWarsActiveZipLineIndex_ >= 0) {
            const tntwars::ZipLineState& zipLine = tntWarsZipLines_[tntWarsActiveZipLineIndex_];
            const tntwars::ZipLineArcLengthTable& table = tntWarsZipLineArcTables_[tntWarsActiveZipLineIndex_];
            float tntWarsZipLineDt = dt * tntWarsTraversalMultiplier;
            if (auto ridePosition =
                    tntwars::advanceZipLineRider(zipLine, table, tntWarsZipLineRider_, tntWarsZipLineDt)) {
                physics.setPosition(character, ecs, *ridePosition);
                physics.setHorizontalVelocity(character, ecs, glm::vec2(0.0f));
                physics.setVerticalVelocity(character, ecs, 0.0f);
                if (tntWarsZipLineRider_.distanceTraveled <= 0.0f ||
                    tntWarsZipLineRider_.distanceTraveled >= table.totalLength) {
                    tntWarsActiveZipLineIndex_ = -1;
                }
            } else {
                tntWarsActiveZipLineIndex_ = -1; // real, degenerate zero-length curve -- bail out
            }
        }

        {
            const std::vector<tntwars::ScavengeNodeState>& nodes = match.scavengeNodes();
            tntwars::TeamId localTeam = match.teamOf(tntWarsLocalPlayerId_);
            int nearestIndex = -1;
            float nearestDist = std::numeric_limits<float>::max();
            constexpr float kScavengeTriggerRadius = 2.5f;
            for (size_t i = 0; i < nodes.size(); ++i) {
                const tntwars::ScavengeNodeState& node = nodes[i];
                if (node.quantityRemaining <= 0) continue;
                tntwars::Territory territory = tntwars::territoryAt(node.position);
                bool ownTerritory = (localTeam == tntwars::TeamId::A && territory == tntwars::Territory::TeamA) ||
                                    (localTeam == tntwars::TeamId::B && territory == tntwars::Territory::TeamB);
                if (!ownTerritory) continue;
                float dist = glm::length(characterPos - node.position);
                if (dist <= kScavengeTriggerRadius && dist < nearestDist) {
                    nearestDist = dist;
                    nearestIndex = static_cast<int>(i);
                }
            }

            if (nearestIndex != tntWarsScavengeHintIndex_) {
                if (nearestIndex >= 0) {
                    std::fprintf(stdout, "[UI hint] Press E to scavenge %s\n",
                                 tntwars::scavengeMaterialName(nodes[static_cast<size_t>(nearestIndex)].material));
                } else {
                    std::fprintf(stdout, "[UI hint] (none)\n");
                }
                tntWarsScavengeHintIndex_ = nearestIndex;
            }

            bool scavengeKeyDown = input.isActionDown("Interact");
            if (nearestIndex >= 0 && scavengeKeyDown && !tntWarsScavengeKeyWasDown_) {
                constexpr int kScavengeAmountPerPress = 5;
                tntwars::ScavengeResult result =
                    match.scavenge(tntWarsLocalPlayerId_, static_cast<size_t>(nearestIndex), kScavengeAmountPerPress);
                if (result.success) {
                    std::fprintf(stdout, "TNT Wars: scavenged %d %s\n", result.quantityCollected,
                                 tntwars::scavengeMaterialName(result.material));
                }
            }
            tntWarsScavengeKeyWasDown_ = scavengeKeyDown;
        }

        for (tntwars::TraversalChallengeState& challenge : tntWarsTraversalChallenges_) {
            tntwars::tickChallengeClock(challenge, dt);
            tntwars::ChallengeTickResult result = tntwars::tickChallengeCheckpoint(challenge, characterPos);
            switch (result) {
                case tntwars::ChallengeTickResult::Started:
                    std::fprintf(stdout, "TNT Wars: challenge \"%s\" started!\n", challenge.name.c_str());
                    break;
                case tntwars::ChallengeTickResult::CheckpointReached:
                    std::fprintf(stdout, "TNT Wars: challenge \"%s\" checkpoint %d/%zu\n", challenge.name.c_str(),
                                 challenge.nextCheckpointIndex, challenge.checkpoints.size());
                    break;
                case tntwars::ChallengeTickResult::Completed:
                    std::fprintf(stdout, "TNT Wars: challenge \"%s\" complete in %.2fs (best %.2fs)\n",
                                 challenge.name.c_str(), challenge.elapsedSeconds, challenge.bestTimeSeconds);
                    break;
                case tntwars::ChallengeTickResult::NoChange:
                    break;
            }
        }

        {
            if (!tntWarsSuitBaseSpeedCaptured_) {
                tntWarsSuitBaseWalkSpeed_ = controller.settings().walkSpeed;
                tntWarsSuitBaseRunSpeed_ = controller.settings().runSpeed;
                tntWarsSuitBaseSpeedCaptured_ = true;
            }

            std::vector<EntityId> nearby = findInteractablesInRange(ecs, characterPos);
            EntityId nearestStation = kNullEntity;
            tntwars::UpgradeCategory nearestCategory = tntwars::UpgradeCategory::Traversal;
            for (EntityId candidate : nearby) {
                if (auto* link = ecs.tryGetComponent<tntwars::UpgradeStationLink>(candidate)) {
                    nearestStation = candidate;
                    nearestCategory = link->category;
                    break;
                }
            }

            if (nearestStation != tntWarsUpgradeHintEntity_) {
                if (nearestStation != kNullEntity) {
                    int currentTier = tntwars::upgradeTier(match.playerUpgrades(tntWarsLocalPlayerId_), nearestCategory);
                    std::fprintf(stdout, "[UI hint] Press E to upgrade %s (tier %d -> %d)\n",
                                 nearestCategory == tntwars::UpgradeCategory::Traversal ? "Traversal" : "Suit",
                                 currentTier, currentTier + 1);
                } else {
                    std::fprintf(stdout, "[UI hint] (none)\n");
                }
                tntWarsUpgradeHintEntity_ = nearestStation;
            }

            bool upgradeKeyDown = input.isActionDown("Interact");
            if (nearestStation != kNullEntity && upgradeKeyDown && !tntWarsUpgradeKeyWasDown_) {
                tntwars::UpgradeResult result = match.purchaseUpgrade(tntWarsLocalPlayerId_, nearestCategory);
                if (result.success) {
                    std::fprintf(stdout, "TNT Wars: upgraded %s to tier %d!\n",
                                 nearestCategory == tntwars::UpgradeCategory::Traversal ? "Traversal" : "Suit",
                                 result.newTier);
                    if (nearestCategory == tntwars::UpgradeCategory::Suit) {
                        float multiplier = tntwars::suitMoveSpeedMultiplier(result.newTier);
                        controller.settingsMutable().walkSpeed = tntWarsSuitBaseWalkSpeed_ * multiplier;
                        controller.settingsMutable().runSpeed = tntWarsSuitBaseRunSpeed_ * multiplier;
                    }
                } else {
                    std::fprintf(stdout, "TNT Wars: upgrade rejected (need more materials, or already maxed)\n");
                }
            }
            tntWarsUpgradeKeyWasDown_ = upgradeKeyDown;
        }
    }

    match.tickScavengeNodes(dt);
    tntwars::tickScavengeNodeVisuals(match.scavengeNodes(), tntWarsScavengeNodeVisuals_, ecs);

    tntwars::tickDestructibleWallVisual(match.trenchesWallMutable(), tntWarsWallVisuals_, ecs, physics, dt);
    tntwars::tickDestructibleWallVisual(match.trenchesCoverMutable(), tntWarsCoverVisuals_, ecs, physics, dt);

    glm::vec3 aimOrigin = camera.position;
    glm::vec3 aimDirection = camera.forward();

    bool placeKeyDown = input.isActionDown("TntWarsPlaceCharge");
    if (placeKeyDown && !tntWarsPlaceTntKeyWasDown_) {
        constexpr float kPlaceMaxDistance = 30.0f;
        Physics::RaycastHit hit = physics.raycast(aimOrigin, aimDirection, kPlaceMaxDistance);
        glm::vec3 placePosition = hit.hit ? hit.point : aimOrigin + aimDirection * kPlaceMaxDistance;
        bool accepted = match.placeTntCharge(tntWarsLocalPlayerId_, placePosition);
        std::fprintf(stdout, "TNT Wars: place charge %s\n", accepted ? "accepted" : "rejected");
    }
    tntWarsPlaceTntKeyWasDown_ = placeKeyDown;

    bool craftKeyDown = input.isActionDown("TntWarsCraftStandardCharge");
    if (craftKeyDown && !tntWarsCraftKeyWasDown_) {
        bool crafted = match.craft(tntWarsLocalPlayerId_, tntwars::ExplosiveRecipeType::StandardCharge);
        std::fprintf(stdout, "TNT Wars: craft Standard Charge %s\n", crafted ? "accepted" : "rejected (need class + materials)");
    }
    tntWarsCraftKeyWasDown_ = craftKeyDown;

    bool placeCraftedKeyDown = input.isActionDown("TntWarsPlaceCraftedCharge");
    if (placeCraftedKeyDown && !tntWarsPlaceCraftedKeyWasDown_) {
        constexpr float kPlaceMaxDistance = 30.0f;
        Physics::RaycastHit hit = physics.raycast(aimOrigin, aimDirection, kPlaceMaxDistance);
        glm::vec3 placePosition = hit.hit ? hit.point : aimOrigin + aimDirection * kPlaceMaxDistance;
        bool accepted = match.placeTntCharge(tntWarsLocalPlayerId_, placePosition, tntwars::ExplosiveRecipeType::StandardCharge);
        std::fprintf(stdout, "TNT Wars: place crafted Standard Charge %s\n", accepted ? "accepted" : "rejected (none crafted yet)");
    }
    tntWarsPlaceCraftedKeyWasDown_ = placeCraftedKeyDown;

    bool grenadeKeyDown = input.isActionDown("TntWarsThrowGrenade");
    if (grenadeKeyDown && !tntWarsGrenadeThrowKeyWasDown_) {
        tntWarsGrenades_.push_back(tntwars::throwGrenade(tntWarsLocalPlayerId_, aimOrigin, aimDirection));
        std::fprintf(stdout, "TNT Wars: grenade thrown\n");
    }
    tntWarsGrenadeThrowKeyWasDown_ = grenadeKeyDown;

    bool settingsKeyDown = input.isActionDown("TntWarsToggleSettingsOverlay");
    if (settingsKeyDown && !tntWarsSettingsKeyWasDown_) tntWarsSettingsOverlayVisible_ = !tntWarsSettingsOverlayVisible_;
    tntWarsSettingsKeyWasDown_ = settingsKeyDown;

    bool leaderboardKeyDown = input.isActionDown("TntWarsToggleLeaderboardOverlay");
    if (leaderboardKeyDown && !tntWarsLeaderboardKeyWasDown_) tntWarsLeaderboardOverlayVisible_ = !tntWarsLeaderboardOverlayVisible_;
    tntWarsLeaderboardKeyWasDown_ = leaderboardKeyDown;

    tntwars::RespawnTickResult respawnResult =
        tntwars::tickRespawn(tntWarsRespawnState_, match.isAlive(tntWarsLocalPlayerId_), dt);
    switch (respawnResult) {
        case tntwars::RespawnTickResult::CountdownStarted:
            std::fprintf(stdout, "TNT Wars: you died -- respawning in %.0fs\n", tntwars::kRespawnSeconds);
            break;
        case tntwars::RespawnTickResult::ReadyToRespawn: {
            if (match.respawnPlayer(tntWarsLocalPlayerId_)) {
                int classIndex = static_cast<int>(match.classOf(tntWarsLocalPlayerId_));
                float angle = static_cast<float>(classIndex) * (6.28318530718f / 5.0f);
                glm::vec3 classOffset(std::cos(angle) * 3.0f, 0.0f, std::sin(angle) * 3.0f);
                physics.setPosition(character, ecs, tntWarsRespawnPosition_ + classOffset);
                physics.setHorizontalVelocity(character, ecs, glm::vec2(0.0f));
                physics.setVerticalVelocity(character, ecs, 0.0f);
                std::fprintf(stdout, "TNT Wars: respawned as %s (%.0f HP)\n",
                             tntwars::playerClassName(match.classOf(tntWarsLocalPlayerId_)),
                             match.health(tntWarsLocalPlayerId_));
            }
            break;
        }
        case tntwars::RespawnTickResult::StillWaiting:
        case tntwars::RespawnTickResult::NoChange:
            break;
    }

    bool fireKeyDown = input.isActionDown("TntWarsFireWeapon");
    if (fireKeyDown && !tntWarsFireKeyWasDown_) {
        tntwars::TntWarsMatch::FireResult result = match.fireWeapon(tntWarsLocalPlayerId_, aimOrigin, aimDirection, ctx.totalSimTime);
        std::fprintf(stdout, "TNT Wars: fire weapon %s\n", result.accepted ? "accepted" : "rejected");
        if (result.accepted) {
            core::EntityId projectileVisual = tntwars::spawnProjectileVisual(
                ecs, meshes, tntWarsMaterials_, renderer.allocator(), renderer.device(),
                renderer.commandPool(), renderer.graphicsQueue(), result.projectile, "TntWarsProjectile");
            tntWarsProjectileVisuals_.push_back({result.projectile, projectileVisual});
        }
    }
    tntWarsFireKeyWasDown_ = fireKeyDown;

    bool ultimateKeyDown = input.isActionDown("TntWarsTriggerUltimate");
    if (ultimateKeyDown && !tntWarsUltimateKeyWasDown_) {
        tntwars::TntWarsMatch::UltimateResult result = match.triggerUltimate(tntWarsLocalPlayerId_, ctx.totalSimTime);
        std::fprintf(stdout, "TNT Wars: trigger ultimate %s\n", result.accepted ? "accepted" : "rejected");
    }
    tntWarsUltimateKeyWasDown_ = ultimateKeyDown;

    const char* classActionNames[5] = {"TntWarsSelectClass1", "TntWarsSelectClass2", "TntWarsSelectClass3",
                                        "TntWarsSelectClass4", "TntWarsSelectClass5"};
    const tntwars::PlayerClassType classTypes[5] = {tntwars::PlayerClassType::Striker, tntwars::PlayerClassType::Deflector,
                                                      tntwars::PlayerClassType::Engineer, tntwars::PlayerClassType::Interceptor,
                                                      tntwars::PlayerClassType::Saboteur};
    for (int i = 0; i < 5; ++i) {
        bool classKeyDown = input.isActionDown(classActionNames[i]);
        if (classKeyDown && !tntWarsClassKeyWasDown_[i]) {
            bool accepted = match.selectClass(tntWarsLocalPlayerId_, classTypes[i]);
            std::fprintf(stdout, "TNT Wars: select class %s %s\n", tntwars::playerClassName(classTypes[i]),
                         accepted ? "accepted" : "rejected");
            if (accepted) match.matchFlow().advanceTo(tntwars::MatchPhase::InProgress);
        }
        tntWarsClassKeyWasDown_[i] = classKeyDown;
    }

    tntWarsMatchElapsedSeconds_ += dt;
    ui.beginFrame(VkExtent2D{ctx.windowWidth, ctx.windowHeight});
    ctx.scriptUi.flushInto(ui);
    ctx.scriptUiLayout.renderInto(ui, glm::vec2(static_cast<float>(ctx.windowWidth), static_cast<float>(ctx.windowHeight)));
    if (match.hasSelectedClass(tntWarsLocalPlayerId_)) {
        tntwars::ClassStats stats = match.classTuning().statsFor(match.classOf(tntWarsLocalPlayerId_));
        float health = match.health(tntWarsLocalPlayerId_);
        float healthFrac = stats.maxHealth > 0.0f ? glm::clamp(health / stats.maxHealth, 0.0f, 1.0f) : 0.0f;
        float ultimateFrac = stats.ultimateChargeRequired > 0.0f
                                  ? glm::clamp(match.ultimateCharge().charge(tntWarsLocalPlayerId_) / stats.ultimateChargeRequired, 0.0f, 1.0f)
                                  : 0.0f;

        constexpr glm::vec2 kBarSize(220.0f, 22.0f);
        constexpr glm::vec2 kBarPos(24.0f, 24.0f);
        ui.drawRect(kBarPos, kBarSize, glm::vec4(0.0f, 0.0f, 0.0f, 0.55f));
        ui.drawRect(kBarPos, glm::vec2(kBarSize.x * healthFrac, kBarSize.y), glm::vec4(0.75f, 0.15f, 0.15f, 0.85f));
        char healthLabel[32];
        std::snprintf(healthLabel, sizeof(healthLabel), "HP %d/%d", static_cast<int>(health), static_cast<int>(stats.maxHealth));
        ui.drawText(healthLabel, kBarPos + glm::vec2(6.0f, 3.0f), 0.75f, glm::vec4(1.0f));

        glm::vec2 ultBarPos = kBarPos + glm::vec2(0.0f, kBarSize.y + 8.0f);
        ui.drawRect(ultBarPos, kBarSize, glm::vec4(0.0f, 0.0f, 0.0f, 0.55f));
        ui.drawRect(ultBarPos, glm::vec2(kBarSize.x * ultimateFrac, kBarSize.y), glm::vec4(0.25f, 0.45f, 0.85f, 0.85f));
        char ultLabel[32];
        std::snprintf(ultLabel, sizeof(ultLabel), "ULT %d%%", static_cast<int>(ultimateFrac * 100.0f));
        ui.drawText(ultLabel, ultBarPos + glm::vec2(6.0f, 3.0f), 0.75f, glm::vec4(1.0f));

        const tntwars::ScavengedMaterials& mats = match.scavengedMaterials(tntWarsLocalPlayerId_);
        char matsLabel[96];
        std::snprintf(matsLabel, sizeof(matsLabel), "%s %d  %s %d  %s %d",
                      tntwars::scavengeMaterialName(tntwars::ScavengeMaterialType::ScrapMetal),
                      tntwars::scavengedMaterialCount(mats, tntwars::ScavengeMaterialType::ScrapMetal),
                      tntwars::scavengeMaterialName(tntwars::ScavengeMaterialType::BlastingPowder),
                      tntwars::scavengedMaterialCount(mats, tntwars::ScavengeMaterialType::BlastingPowder),
                      tntwars::scavengeMaterialName(tntwars::ScavengeMaterialType::Wiring),
                      tntwars::scavengedMaterialCount(mats, tntwars::ScavengeMaterialType::Wiring));
        ui.drawText(matsLabel, kBarPos + glm::vec2(0.0f, (kBarSize.y + 8.0f) * 2.0f), 0.65f, glm::vec4(0.9f, 0.9f, 0.85f, 1.0f));

        const tntwars::CraftedExplosives& crafted = match.craftedExplosives(tntWarsLocalPlayerId_);
        char craftLabel[64];
        std::snprintf(craftLabel, sizeof(craftLabel), "Crafted Charges: %d  [C]raft  [H]place",
                      crafted.counts[static_cast<size_t>(tntwars::ExplosiveRecipeType::StandardCharge)]);
        ui.drawText(craftLabel, kBarPos + glm::vec2(0.0f, (kBarSize.y + 8.0f) * 2.0f + 22.0f), 0.6f,
                              glm::vec4(1.0f, 0.75f, 0.4f, 1.0f));

        const tntwars::PlayerUpgrades& upgrades = match.playerUpgrades(tntWarsLocalPlayerId_);
        char upgradeLabel[64];
        std::snprintf(upgradeLabel, sizeof(upgradeLabel), "Traversal Lv%d  Suit Lv%d", upgrades.traversalTier, upgrades.suitTier);
        ui.drawText(upgradeLabel, kBarPos + glm::vec2(0.0f, (kBarSize.y + 8.0f) * 2.0f + 44.0f), 0.65f,
                              glm::vec4(0.7f, 0.85f, 1.0f, 1.0f));
    } else {
        ui.drawText("SELECT YOUR CLASS", glm::vec2(24.0f, 24.0f), 0.85f, glm::vec4(1.0f, 0.85f, 0.3f, 1.0f));
        for (int i = 0; i < 5; ++i) {
            char classLine[64];
            std::snprintf(classLine, sizeof(classLine), "[%d] %s", i + 1, tntwars::playerClassName(classTypes[i]));
            ui.drawText(classLine, glm::vec2(24.0f, 60.0f + static_cast<float>(i) * 26.0f), 0.7f,
                                  glm::vec4(0.9f, 0.9f, 0.95f, 1.0f));
        }
    }

    {
        float hudY = 24.0f;
        for (const tntwars::TraversalChallengeState& challenge : tntWarsTraversalChallenges_) {
            if (!challenge.running) continue;
            char label[128];
            std::snprintf(label, sizeof(label), "%s: %.1fs", challenge.name.c_str(), challenge.elapsedSeconds);
            glm::vec2 size = ui.measureText(label, 0.7f);
            ui.drawText(label, glm::vec2(static_cast<float>(ctx.windowWidth) - size.x - 24.0f, hudY), 0.7f,
                                  glm::vec4(0.6f, 1.0f, 0.7f, 1.0f));
            hudY += 26.0f;
        }
    }

    for (size_t i = 0; i < tntWarsPvPNodes_.size(); ++i) {
        const tntwars::PvPNodeState& node = tntWarsPvPNodes_[i];
        if (glm::length(characterPos - node.position) > node.radius) continue;
        char label[96];
        const char* controlledBy = node.controllingTeam.has_value()
                                        ? (*node.controllingTeam == tntwars::TeamId::A ? "Team A" : "Team B")
                                        : "Neutral";
        std::snprintf(label, sizeof(label), "PvP Node: %s (%.0f%%)", controlledBy, node.captureProgress * 100.0f);
        glm::vec2 size = ui.measureText(label, 0.75f);
        ui.drawText(label, glm::vec2((static_cast<float>(ctx.windowWidth) - size.x) * 0.5f, 24.0f), 0.75f,
                              glm::vec4(1.0f, 0.9f, 0.4f, 1.0f));
        break;
    }

    if (tntWarsSettingsOverlayVisible_) {
        glm::vec2 panelPos(24.0f, static_cast<float>(ctx.windowHeight) * 0.5f - 110.0f);
        glm::vec2 panelSize(360.0f, 220.0f);
        ui.drawRect(panelPos, panelSize, glm::vec4(0.0f, 0.0f, 0.0f, 0.72f));
        glm::vec2 cursor = panelPos + glm::vec2(16.0f, 14.0f);
        ui.drawText("SETTINGS  (F1 to close)", cursor, 0.75f, glm::vec4(1.0f, 0.85f, 0.3f, 1.0f));
        cursor.y += 32.0f;
        auto drawToggleLine = [&](const char* label, bool enabled, const char* key) {
            char line[96];
            std::snprintf(line, sizeof(line), "%-22s [%s]  %s", label, enabled ? "ON " : "OFF", key);
            ui.drawText(line, cursor, 0.62f,
                                  enabled ? glm::vec4(0.6f, 1.0f, 0.7f, 1.0f) : glm::vec4(0.7f, 0.7f, 0.75f, 1.0f));
            cursor.y += 24.0f;
        };
        drawToggleLine("RT Reflections", renderer.isRTReflectionsEnabled(), "F9");
        drawToggleLine("Volumetric Fog", renderer.isVolumetricFogEnabled(), "F11");
        drawToggleLine("Screen-Space Reflections", renderer.isSSREnabled(), "--");
        drawToggleLine("Ray-Traced GI", renderer.isRTGIEnabled(), "--");
        drawToggleLine("Cinematic Mode", renderer.isCinematicModeEnabled(), "F7");
        drawToggleLine("Heat Distortion", renderer.isHeatDistortionEnabled(), "F8");
    }

    if (tntWarsLeaderboardOverlayVisible_) {
        glm::vec2 panelPos(static_cast<float>(ctx.windowWidth) * 0.5f - 220.0f, 90.0f);
        glm::vec2 panelSize(440.0f, 90.0f + static_cast<float>(tntWarsTraversalChallenges_.size()) * 24.0f +
                                         static_cast<float>(tntWarsPvPNodes_.size()) * 24.0f);
        ui.drawRect(panelPos, panelSize, glm::vec4(0.0f, 0.0f, 0.0f, 0.72f));
        glm::vec2 cursor = panelPos + glm::vec2(16.0f, 14.0f);
        ui.drawText("LEADERBOARD  (Tab to close)", cursor, 0.75f, glm::vec4(1.0f, 0.85f, 0.3f, 1.0f));
        cursor.y += 32.0f;
        ui.drawText("Best Race Times:", cursor, 0.65f, glm::vec4(0.8f, 0.9f, 1.0f, 1.0f));
        cursor.y += 24.0f;
        for (const tntwars::TraversalChallengeState& challenge : tntWarsTraversalChallenges_) {
            char line[96];
            if (challenge.bestTimeSeconds >= 0.0f) {
                std::snprintf(line, sizeof(line), "  %s: %.2fs", challenge.name.c_str(), challenge.bestTimeSeconds);
            } else {
                std::snprintf(line, sizeof(line), "  %s: --", challenge.name.c_str());
            }
            ui.drawText(line, cursor, 0.6f, glm::vec4(0.9f, 0.9f, 0.9f, 1.0f));
            cursor.y += 24.0f;
        }
        cursor.y += 8.0f;
        ui.drawText("PvP Nodes:", cursor, 0.65f, glm::vec4(0.8f, 0.9f, 1.0f, 1.0f));
        cursor.y += 24.0f;
        for (size_t i = 0; i < tntWarsPvPNodes_.size(); ++i) {
            const tntwars::PvPNodeState& node = tntWarsPvPNodes_[i];
            char line[96];
            const char* controlledBy = node.controllingTeam.has_value()
                                            ? (*node.controllingTeam == tntwars::TeamId::A ? "Team A" : "Team B")
                                            : "Neutral";
            std::snprintf(line, sizeof(line), "  Node %zu: %s", i, controlledBy);
            ui.drawText(line, cursor, 0.6f, glm::vec4(0.9f, 0.9f, 0.9f, 1.0f));
            cursor.y += 24.0f;
        }
    }

    {
        constexpr float kChatWidth = 420.0f;
        constexpr float kLineHeight = 20.0f;
        float chatBottom = static_cast<float>(ctx.windowHeight) - 24.0f;
        if (tntWarsChatFocused_) {
            glm::vec2 inputPos(24.0f, chatBottom - kLineHeight);
            ui.drawRect(inputPos, glm::vec2(kChatWidth, kLineHeight + 6.0f), glm::vec4(0.0f, 0.0f, 0.0f, 0.75f));
            std::string inputLine = "> " + tntWarsChatInputBuffer_ + "_";
            ui.drawText(inputLine, inputPos + glm::vec2(6.0f, 3.0f), 0.6f, glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
            chatBottom -= (kLineHeight + 10.0f);
        }
        size_t historyCount = std::min(tntWarsChatHistory_.size(), kTntWarsChatVisibleLines);
        if (historyCount > 0) {
            glm::vec2 panelPos(24.0f, chatBottom - static_cast<float>(historyCount) * kLineHeight - 8.0f);
            glm::vec2 panelSize(kChatWidth, static_cast<float>(historyCount) * kLineHeight + 12.0f);
            ui.drawRect(panelPos, panelSize, glm::vec4(0.0f, 0.0f, 0.0f, 0.45f));
            glm::vec2 cursor = panelPos + glm::vec2(8.0f, 6.0f);
            size_t start = tntWarsChatHistory_.size() - historyCount;
            for (size_t i = start; i < tntWarsChatHistory_.size(); ++i) {
                const TntWarsChatEntry& entry = tntWarsChatHistory_[i];
                std::string line = "[" + entry.senderName + "]: " + entry.body;
                ui.drawText(line, cursor, 0.55f, glm::vec4(0.95f, 0.95f, 0.95f, 1.0f));
                cursor.y += kLineHeight;
            }
        }
    }

    if (tntWarsMatchElapsedSeconds_ < 30.0f) {
        constexpr const char* kTutorialHints[] = {
            "WASD to move, mouse to look, Space to jump, Shift to sprint",
            "G places a TNT charge, V throws a grenade -- both real, physical explosives",
            "F fires your class weapon, Q triggers your ultimate once charged",
            "E scavenges resource nodes, mounts zip-lines, and spends materials at upgrade stations",
            "F1 opens Settings, Tab opens the Leaderboard",
        };
        constexpr int kHintCount = static_cast<int>(sizeof(kTutorialHints) / sizeof(kTutorialHints[0]));
        int hintIndex = std::min(static_cast<int>(tntWarsMatchElapsedSeconds_ / 6.0f), kHintCount - 1);
        const char* hint = kTutorialHints[hintIndex];
        glm::vec2 hintSize = ui.measureText(hint, 0.65f);
        glm::vec2 hintPos((static_cast<float>(ctx.windowWidth) - hintSize.x) * 0.5f,
                           static_cast<float>(ctx.windowHeight) - 56.0f);
        ui.drawRect(hintPos + glm::vec2(-12.0f, -8.0f), hintSize + glm::vec2(24.0f, 16.0f),
                              glm::vec4(0.0f, 0.0f, 0.0f, 0.6f));
        ui.drawText(hint, hintPos, 0.65f, glm::vec4(1.0f, 1.0f, 0.85f, 1.0f));
    }}

} // namespace engine::tntwars
