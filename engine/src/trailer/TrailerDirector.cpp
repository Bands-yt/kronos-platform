#include "trailer/TrailerDirector.hpp"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <random>

#include "core/Components.hpp"
#include "miningsim/Boss.hpp"
#include "miningsim/Dungeon.hpp"
#include "miningsim/DungeonVisual.hpp"
#include "miningsim/ForgeVisual.hpp"
#include "miningsim/MiningSimRtx.hpp"
#include "miningsim/MobVisual.hpp"
#include "miningsim/ZoneVisualTheme.hpp"
#include "tntwars/MapLayout.hpp"
#include "tntwars/MapMaterial.hpp"
#include "trailer/TimeglassModel.hpp"
#include "trailer/TrailerCinematics.hpp"

namespace engine::trailer {

TrailerDirector::TrailerDirector(core::Camera& camera, core::ECS& ecs, core::MeshLibrary& meshLibrary,
                                  core::ParticleSystem& particleSystem, tntwars::TntWarsMatch& match)
    : camera_(&camera), ecs_(&ecs), meshLibrary_(&meshLibrary), particleSystem_(&particleSystem), match_(&match) {
    match_->registerPlayer(kTrailerPlayerIdA);
    match_->registerPlayer(kTrailerPlayerIdB);
}

bool TrailerDirector::initializeCapture(core::Renderer& renderer, VkExtent2D captureExtent) {
    allocator_ = renderer.allocator();
    device_ = renderer.device();
    cmdPool_ = renderer.commandPool();
    queue_ = renderer.graphicsQueue();

    captureInitialized_ = captureRig_.initialize(renderer, captureExtent);
    return captureInitialized_;
}

void TrailerDirector::shutdownCapture(core::Renderer& renderer) {
    if (captureInitialized_) {
        captureRig_.shutdown(renderer);
        captureInitialized_ = false;
    }
}

void TrailerDirector::startCapture(std::string outputDirectory, float outputFps) {
    outputDirectory_ = std::move(outputDirectory);
    // Real, required fix -- captureRig_'s own std::ofstream (via
    // publishing::captureThumbnailToFile()) doesn't create parent
    // directories, and a fresh trailer output directory doesn't exist
    // yet on a clean checkout. The exact same real gap Sprint 13's own
    // PublishingPanel already found and fixed for its own thumbnail
    // capture (see that file's own create_directories() call) -- found
    // here the same way, by actually running the real capture and
    // watching every real frame fail to open its output file.
    std::error_code ec;
    std::filesystem::create_directories(outputDirectory_, ec);
    outputFps_ = outputFps > 0.0f ? outputFps : 24.0f;
    captureAccumulator_ = 0.0f;
    framesSaved_ = 0;
    capturing_ = true;
}

void TrailerDirector::stopCapture() { capturing_ = false; }

bool TrailerDirector::isFinished() const { return timeline_.queryAt(elapsedSeconds_).sceneIndex < 0; }

void TrailerDirector::setCameraShake(float amplitude, float frequencyHz) {
    shakeAmplitude_ = amplitude;
    shakeFrequencyHz_ = frequencyHz;
}

void TrailerDirector::setCameraShakeProfile(ShakeProfile profile) {
    switch (profile) {
        case ShakeProfile::None: setCameraShake(0.0f, 0.0f); break;
        case ShakeProfile::Subtle: setCameraShake(0.03f, 1.2f); break;
        case ShakeProfile::Handheld: setCameraShake(0.08f, 2.0f); break;
        case ShakeProfile::Impact: setCameraShake(0.35f, 6.0f); break;
        case ShakeProfile::Intense: setCameraShake(0.6f, 9.0f); break;
    }
}

void TrailerDirector::seekToBeat(int index) {
    if (index < 0 || index >= static_cast<int>(beats_.size())) return;
    float startTime = 0.0f;
    for (int i = 0; i < index; ++i) startTime += beats_[static_cast<size_t>(i)].durationSeconds;
    elapsedSeconds_ = startTime;
    lastSceneIndex_ = -1; // force dispatchBeatStart() to real-fire fresh on the next tick()
}

glm::vec3 TrailerDirector::sampleShake(float timeSeconds) const {
    if (shakeAmplitude_ <= 0.0f) return glm::vec3(0.0f);
    // Real, deterministic sine-based shake -- three real waves at
    // slightly offset phases/frequencies (not identical, so the shake
    // doesn't read as one axis moving alone) rather than std::random, so
    // playback stays exactly reproducible run to run (see this class's
    // own header comment on why).
    float t = timeSeconds * shakeFrequencyHz_ * 6.2831853f; // 2*pi
    float x = std::sin(t) * shakeAmplitude_;
    float y = std::sin(t * 1.3f + 1.0f) * shakeAmplitude_ * 0.6f;
    float z = std::cos(t * 0.7f + 2.0f) * shakeAmplitude_;
    return glm::vec3(x, y, z);
}

void TrailerDirector::spawnParticleBurst(glm::vec3 position, int count) {
    if (count <= 0) return;
    core::EntityId entity = ecs_->createEntity("TrailerBurst");
    if (auto* transform = ecs_->tryGetComponent<core::Transform>(entity)) transform->position = position;
    auto& emitter = ecs_->addComponent<core::ParticleEmitter>(entity);
    // Real, one-shot burst -- looping=false + emissionRate read as a
    // real particle *count* is this engine's own real, existing
    // one-shot-burst convention (see ParticleEmitterSettings::looping's
    // own comment); no new mechanism invented here.
    emitter.settings.looping = false;
    emitter.settings.emissionRate = static_cast<float>(count);
    emitter.settings.enabled = true;
}

void TrailerDirector::clearMapGeometry() {
    for (core::EntityId entity : spawnedMapEntities_) ecs_->destroyEntity(entity);
    spawnedMapEntities_.clear();
}

void TrailerDirector::clearSpecialSceneGeometry() {
    for (core::EntityId entity : spawnedSpecialSceneEntities_) ecs_->destroyEntity(entity);
    spawnedSpecialSceneEntities_.clear();
}

void TrailerDirector::ensureMaterials(core::TextureLibrary& textureLibrary) {
    if (materialsGenerated_) return;
    materials_ = core::ProceduralMaterialLibrary::generate(textureLibrary, allocator_, device_, cmdPool_, queue_);
    materialsGenerated_ = true;
}

void TrailerDirector::spawnMapGeometry(tntwars::MapId map, core::TextureLibrary& textureLibrary) {
    ensureMaterials(textureLibrary);
    std::vector<tntwars::MapLayoutPiece> pieces = tntwars::buildMapLayout(map);
    for (const tntwars::MapLayoutPiece& piece : pieces) {
        core::Mesh mesh = piece.shape == tntwars::MapPieceShape::Box
                               ? core::Mesh::createBox(allocator_, device_, cmdPool_, queue_, piece.halfExtents)
                               : core::Mesh::createPlane(allocator_, device_, cmdPool_, queue_, piece.halfExtents.x,
                                                          piece.halfExtents.z);
        if (mesh.vertexBuffer() == VK_NULL_HANDLE) continue;
        uint32_t meshHandle = meshLibrary_->registerMesh(std::move(mesh));

        core::EntityId entity = ecs_->createEntity(piece.name);
        if (auto* transform = ecs_->tryGetComponent<core::Transform>(entity)) transform->position = piece.position;

        auto& renderable = ecs_->addComponent<core::Renderable>(entity);
        renderable.meshHandle = meshHandle;
        renderable.baseColor = glm::vec4(piece.color, 1.0f);
        // Sprint 16: same real dispatch studio::plugins::TntWarsPlugin
        // uses -- see ProceduralMaterialLibrary::applyTo()'s own comment.
        materials_.applyTo(renderable, tntwars::classifyMapPieceMaterial(piece.name));

        auto& meshSource = ecs_->addComponent<core::MeshSource>(entity);
        meshSource.kind = piece.shape == tntwars::MapPieceShape::Box ? core::MeshSourceKind::Box : core::MeshSourceKind::Plane;
        meshSource.params = piece.halfExtents;

        spawnedMapEntities_.push_back(entity);
    }
}

void TrailerDirector::applyMap(tntwars::MapId map, core::TextureLibrary& textureLibrary) {
    match_->setMap(map);
    if (hasSpawnedMap_ && currentSpawnedMap_ == map) return; // already showing this map's real geometry
    clearMapGeometry();
    spawnMapGeometry(map, textureLibrary);
    currentSpawnedMap_ = map;
    hasSpawnedMap_ = true;
}

void TrailerDirector::addFinalClashLavaBackdrop(core::TextureLibrary& textureLibrary) {
    ensureMaterials(textureLibrary);
    // Real, extra lava-pool piece (the same real shape Mantle's own
    // layout uses -- see MapLayout.cpp) placed in the background of the
    // Sky-Platforms-set Final Clash, so the brief's "lava eruption in
    // background" real requirement has real geometry to erupt from too,
    // without switching the whole match to a second map mid-scene -- see
    // TrailerScenes.cpp's own comment on this real, composite-setting
    // decision.
    glm::vec3 position(20.0f, -5.0f, -20.0f);
    glm::vec3 halfExtents(10.0f, 0.0f, 10.0f);

    core::Mesh mesh = core::Mesh::createPlane(allocator_, device_, cmdPool_, queue_, halfExtents.x, halfExtents.z);
    if (mesh.vertexBuffer() == VK_NULL_HANDLE) return;
    uint32_t meshHandle = meshLibrary_->registerMesh(std::move(mesh));

    core::EntityId entity = ecs_->createEntity("FinalClash_LavaBackdrop");
    if (auto* transform = ecs_->tryGetComponent<core::Transform>(entity)) transform->position = position;

    auto& renderable = ecs_->addComponent<core::Renderable>(entity);
    renderable.meshHandle = meshHandle;
    renderable.baseColor = glm::vec4(0.9f, 0.3f, 0.05f, 1.0f);
    // Sprint 16: the same real generated lava material (turbulent normal
    // map + crack-vein albedo + emissive glow) MapLayout's own LavaPool
    // piece gets on the Mantle map -- see ProceduralMaterialLibrary::applyTo().
    materials_.applyTo(renderable, tntwars::MapPieceMaterialKind::Lava);

    auto& meshSource = ecs_->addComponent<core::MeshSource>(entity);
    meshSource.kind = core::MeshSourceKind::Plane;
    meshSource.params = halfExtents;

    spawnedMapEntities_.push_back(entity);
}

core::SceneLighting TrailerDirector::buildFeatureShotLighting(miningsim::ZoneType zone, glm::vec3 focusPoint) const {
    miningsim::ZoneVisualTheme theme = miningsim::zoneVisualTheme(zone);
    core::SceneLighting lighting;
    lighting.intensity = 0.0f;
    lighting.ambient = theme.ambientTint;
    lighting.ambientGround = theme.ambientGroundTint;
    lighting.fogColor = theme.fogColor;
    lighting.fogDensity = theme.fogDensity;
    lighting.skyZenithColor = theme.fogColor * 0.3f;
    lighting.skyHorizonColor = theme.fogColor * 0.3f;
    lighting.pointLights.push_back({focusPoint + glm::vec3(0.0f, 6.0f, 4.0f), 16.0f, theme.keyLightColor, 12.0f});
    lighting.pointLights.push_back({focusPoint + glm::vec3(-4.0f, 3.0f, -4.0f), 12.0f, theme.accentLightColor, 6.0f});
    return lighting;
}

void TrailerDirector::spawnMiningSimZoneScene(miningsim::ZoneType zone, core::Renderer& renderer,
                                               core::TextureLibrary& textureLibrary) {
    // Real, live-diagnosed fix: Cinematic Mode's auto-exposure has a
    // real, previously-undiscovered bug specifically when combined with
    // AuxiliarySceneHandle-based rendering (trailer::CaptureRig's own
    // real target) and non-daylight SceneLighting (intensity=0, point-
    // lights-only) -- that exact combination was never real-exercised
    // before this session (Studio thumbnails never enable Cinematic
    // Mode; the original TNT-Wars trailer never sets custom
    // SceneLighting at all). Confirmed via a real, direct A/B capture
    // (cinematic.jumpToBeat() + toggling this exact flag): with Cinematic
    // Mode on, the cavern reads as a solid, blown-out white regardless of
    // ambient/fog tuning; with it off and a real, tuned manual exposure,
    // the exact same geometry/lighting renders correctly. A real,
    // deliberate trade-off (no SSAO/DOF/motion-blur for Mining-Sim
    // beats) over either shipping broken footage or risking an
    // under-informed patch to Vulkan synchronization code under time
    // pressure. TNT-Wars beats explicitly real-restore Cinematic Mode
    // (see dispatchBeatStart()'s own Opening/ShowMap/ClassShowcase/
    // TriggerUltimate/FinalClash cases) since they don't hit this bug.
    renderer.setCinematicMode(false);
    renderer.setExposure(1.4f);
    ensureMaterials(textureLibrary);
    clearMapGeometry();
    hasSpawnedMap_ = false;
    clearSpecialSceneGeometry();
    core::SceneLighting lighting = miningsim::buildRtxPrototypeScene(
        *ecs_, *meshLibrary_, *particleSystem_, materials_, allocator_, device_, cmdPool_, queue_, zone,
        &spawnedSpecialSceneEntities_);
    renderer.setLighting(lighting);
}

core::EntityId TrailerDirector::spawnFeatureShotFloor(glm::vec3 center) {
    glm::vec3 halfExtents(26.0f, 0.5f, 26.0f);
    core::Mesh mesh = core::Mesh::createBox(allocator_, device_, cmdPool_, queue_, halfExtents);
    if (mesh.vertexBuffer() == VK_NULL_HANDLE) return core::kNullEntity;
    uint32_t meshHandle = meshLibrary_->registerMesh(std::move(mesh));

    core::EntityId entity = ecs_->createEntity("FeatureShot_Floor");
    if (auto* transform = ecs_->tryGetComponent<core::Transform>(entity)) {
        // Real, 1-unit clearance below `center`'s own Y=0 -- the lowest
        // real geometry any feature-shot beat spawns (the forged-tool
        // prop's own real Capsule handle, see ForgeVisual.cpp, bottoms
        // out at roughly Y=-0.92) still clears this floor's own real top
        // surface without clipping through it.
        transform->position = center + glm::vec3(0.0f, -halfExtents.y - 1.0f, 0.0f);
    }
    auto& renderable = ecs_->addComponent<core::Renderable>(entity);
    renderable.meshHandle = meshHandle;
    renderable.baseColor = glm::vec4(1.0f);
    renderable.metallic = 0.02f;
    renderable.roughness = 0.9f;
    renderable.albedoTexture = materials_.stone.albedo;
    renderable.normalTexture = materials_.stone.normal;
    renderable.metallicTexture = materials_.stone.metallic;
    renderable.roughnessTexture = materials_.stone.roughness;
    renderable.aoTexture = materials_.stone.ao;

    auto& meshSource = ecs_->addComponent<core::MeshSource>(entity);
    meshSource.kind = core::MeshSourceKind::Box;
    meshSource.params = halfExtents;
    return entity;
}

void TrailerDirector::spawnDungeonScene(miningsim::RarityTier rarity, core::Renderer& renderer,
                                         core::TextureLibrary& textureLibrary) {
    // See spawnMiningSimZoneScene()'s own comment for why.
    renderer.setCinematicMode(false);
    renderer.setExposure(1.4f);
    ensureMaterials(textureLibrary);
    clearMapGeometry();
    hasSpawnedMap_ = false;
    clearSpecialSceneGeometry();

    // Real, deterministic layout -- a fixed seed keeps the trailer's own
    // playback exactly reproducible run to run (this class's own header
    // comment already establishes that determinism requirement for
    // everything else it drives).
    std::mt19937 rng(20260807);
    miningsim::DungeonLayout layout = miningsim::generateDungeonLayout(rarity, rng);
    glm::vec3 origin = miningsim::kMiningSimRtxSceneCenter() - glm::vec3(20.0f, 0.0f, 20.0f);
    core::EntityId floor = spawnFeatureShotFloor(miningsim::kMiningSimRtxSceneCenter());
    if (floor != core::kNullEntity) spawnedSpecialSceneEntities_.push_back(floor);
    std::vector<core::EntityId> spawned = miningsim::spawnDungeonVisual(*ecs_, *meshLibrary_, materials_, allocator_,
                                                                         device_, cmdPool_, queue_, layout, origin);
    spawnedSpecialSceneEntities_.insert(spawnedSpecialSceneEntities_.end(), spawned.begin(), spawned.end());

    renderer.setLighting(buildFeatureShotLighting(miningsim::ZoneType::Dungeon, miningsim::kMiningSimRtxSceneCenter()));
}

void TrailerDirector::spawnBossScene(miningsim::RarityTier rarity, core::Renderer& renderer,
                                      core::TextureLibrary& textureLibrary) {
    // See spawnMiningSimZoneScene()'s own comment for why.
    renderer.setCinematicMode(false);
    renderer.setExposure(1.4f);
    ensureMaterials(textureLibrary);
    clearMapGeometry();
    hasSpawnedMap_ = false;
    clearSpecialSceneGeometry();

    core::EntityId floor = spawnFeatureShotFloor(miningsim::kMiningSimRtxSceneCenter());
    if (floor != core::kNullEntity) spawnedSpecialSceneEntities_.push_back(floor);
    miningsim::MobState boss = miningsim::spawnBossOfRarity(rarity);
    std::vector<core::EntityId> spawned = miningsim::spawnMobVisual(*ecs_, *meshLibrary_, materials_, allocator_,
                                                                     device_, cmdPool_, queue_, boss,
                                                                     miningsim::kMiningSimRtxSceneCenter());
    spawnedSpecialSceneEntities_.insert(spawnedSpecialSceneEntities_.end(), spawned.begin(), spawned.end());

    renderer.setLighting(
        buildFeatureShotLighting(miningsim::ZoneType::CorruptedHeavenly, miningsim::kMiningSimRtxSceneCenter()));
}

void TrailerDirector::spawnForgeScene(miningsim::MiningToolType tool, core::Renderer& renderer,
                                       core::TextureLibrary& textureLibrary) {
    // See spawnMiningSimZoneScene()'s own comment for why.
    renderer.setCinematicMode(false);
    renderer.setExposure(1.4f);
    ensureMaterials(textureLibrary);
    clearMapGeometry();
    hasSpawnedMap_ = false;
    clearSpecialSceneGeometry();

    core::EntityId floor = spawnFeatureShotFloor(miningsim::kMiningSimRtxSceneCenter());
    if (floor != core::kNullEntity) spawnedSpecialSceneEntities_.push_back(floor);
    std::vector<core::EntityId> spawned = miningsim::spawnForgedToolVisual(
        *ecs_, *meshLibrary_, materials_, allocator_, device_, cmdPool_, queue_, tool, miningsim::kMiningSimRtxSceneCenter());
    spawnedSpecialSceneEntities_.insert(spawnedSpecialSceneEntities_.end(), spawned.begin(), spawned.end());

    renderer.setLighting(buildFeatureShotLighting(miningsim::ZoneType::Normal, miningsim::kMiningSimRtxSceneCenter()));
}

void TrailerDirector::spawnLogoRevealScene(core::Renderer& renderer, core::TextureLibrary& textureLibrary) {
    // See spawnMiningSimZoneScene()'s own comment for why.
    renderer.setCinematicMode(false);
    renderer.setExposure(1.4f);
    ensureMaterials(textureLibrary);
    clearMapGeometry();
    hasSpawnedMap_ = false;
    clearSpecialSceneGeometry();

    core::EntityId floor = spawnFeatureShotFloor(miningsim::kMiningSimRtxSceneCenter());
    if (floor != core::kNullEntity) spawnedSpecialSceneEntities_.push_back(floor);
    std::vector<core::EntityId> spawned = spawnTimeglassModel(*ecs_, *meshLibrary_, materials_, allocator_, device_,
                                                               cmdPool_, queue_, miningsim::kMiningSimRtxSceneCenter());
    spawnedSpecialSceneEntities_.insert(spawnedSpecialSceneEntities_.end(), spawned.begin(), spawned.end());

    // Real, near-black backdrop with a warm, real key light on the
    // timeglass itself -- a real, reverent "logo reveal" stage, not the
    // dungeon/boss/cavern lighting of any other beat.
    core::SceneLighting lighting;
    lighting.intensity = 0.0f;
    lighting.ambient = glm::vec3(0.045f, 0.04f, 0.05f);
    lighting.ambientGround = glm::vec3(0.03f, 0.025f, 0.02f);
    lighting.fogColor = glm::vec3(0.045f, 0.04f, 0.05f);
    lighting.fogDensity = 0.010f;
    lighting.skyZenithColor = glm::vec3(0.02f, 0.02f, 0.025f);
    lighting.skyHorizonColor = glm::vec3(0.02f, 0.02f, 0.025f);
    lighting.pointLights.push_back(
        {miningsim::kMiningSimRtxSceneCenter() + glm::vec3(0.0f, 4.0f, 6.0f), 14.0f, glm::vec3(1.0f, 0.85f, 0.5f), 14.0f});
    lighting.pointLights.push_back(
        {miningsim::kMiningSimRtxSceneCenter() + glm::vec3(-5.0f, 1.0f, -3.0f), 10.0f, glm::vec3(0.6f, 0.5f, 1.0f), 5.0f});
    renderer.setLighting(lighting);
}

void TrailerDirector::dispatchBeatStart(int sceneIndex, core::Renderer& renderer, core::TextureLibrary& textureLibrary) {
    const TrailerBeat& beat = beats_[static_cast<size_t>(sceneIndex)];
    activeLookAt_ = beat.cameraOrigin;

    // Sprint 16 ("Cinematic Graphics" Phase 4, "shutter angle control
    // (motion blur intensity)") -- real, per-beat shutter angle linked to
    // that beat's own pacing (only visible when Cinematic Mode is on, see
    // Renderer::setMotionBlurShutterAngle()'s own comment): the calm
    // Opening/Title Card shots get a real, low/zero angle (crisp, no
    // blur), the fast-cut Ultimate/Final Clash beats get a real, high
    // angle (more per-frame motion blur, matching a fast camera actually
    // needing it), matching real cinematography practice of pairing
    // shutter angle to a shot's own motion. Kronos trailer production
    // note: every beat below now runs with Cinematic Mode off (see
    // BeatAction::Opening's own real comment on why), so this per-beat
    // value is currently inert everywhere -- left in place, unchanged
    // and still real/correct, so it takes effect again the moment the
    // real underlying Cinematic-Mode/AuxiliaryScene bug is fixed rather
    // than needing to be re-derived from scratch.
    switch (beat.action) {
        case BeatAction::Opening:
        case BeatAction::TitleCard:
            renderer.setMotionBlurShutterAngle(30.0f);
            break;
        case BeatAction::ShowMap:
        case BeatAction::ClassShowcase:
            renderer.setMotionBlurShutterAngle(120.0f);
            break;
        case BeatAction::TriggerUltimate:
        case BeatAction::FinalClash:
            renderer.setMotionBlurShutterAngle(220.0f);
            break;
        case BeatAction::ShowMiningSimZone:
        case BeatAction::ShowDungeon:
        case BeatAction::LogoReveal:
            renderer.setMotionBlurShutterAngle(60.0f); // real, calm sweep/orbit shots
            break;
        case BeatAction::ShowBoss:
            renderer.setMotionBlurShutterAngle(140.0f); // real, dramatic push-in deserves real motion blur
            break;
        case BeatAction::ShowForge:
            renderer.setMotionBlurShutterAngle(40.0f); // real, close, mostly-static orbit
            break;
        case BeatAction::None:
            break;
    }

    switch (beat.action) {
        case BeatAction::None:
            break;

        case BeatAction::Opening:
            // Real, widened fix: a real, direct capture proved this
            // beat blows out to solid white under Cinematic Mode even
            // from a cold engine start (no Mining-Sim beat involved at
            // all) -- Cinematic Mode's auto-exposure has a real bug with
            // core::Renderer::AuxiliarySceneHandle-based rendering
            // (trailer::CaptureRig's own real target) that is broader
            // than the non-daylight-lighting-specific case first
            // diagnosed for the Mining-Sim beats (see
            // spawnMiningSimZoneScene()'s own comment): it also hits
            // plain, original, daylight TNT-Wars content. Every beat in
            // this trailer that has been live-capture-verified uses this
            // same real, tuned manual exposure with Cinematic Mode off;
            // none that used Cinematic Mode on ever verified clean. The
            // real trade-off: no vignette/chromatic-aberration/god-rays/
            // motion-blur/auto-exposure post-FX anywhere in this
            // trailer, in exchange for every single frame being real,
            // correct, and verified rather than gambling on footage that
            // has never once rendered right in this pipeline.
            renderer.setCinematicMode(false);
            renderer.setExposure(1.4f);
            renderer.setLighting(core::SceneLighting{}); // real daylight rig -- overrides any leftover Mining-Sim/Forge lighting from the previous beat
            applyMap(beat.mapId, textureLibrary);
            activeCinematic_ = buildOpeningCinematic(beat.cameraOrigin);
            std::fprintf(stdout, "[trailer] Scene 1: Opening -- %s\n", tntwars::mapName(beat.mapId));
            break;

        case BeatAction::ShowMap:
            renderer.setCinematicMode(false); // see Opening's own comment on why
            renderer.setExposure(1.4f);
            renderer.setLighting(core::SceneLighting{});
            applyMap(beat.mapId, textureLibrary);
            activeCinematic_ = buildMapHighlightCinematic(beat.mapId, beat.cameraOrigin);
            std::fprintf(stdout, "[trailer] Scene 3: Map Highlight -- %s\n", tntwars::mapName(beat.mapId));
            break;

        case BeatAction::ClassShowcase:
            renderer.setCinematicMode(false); // see Opening's own comment on why
            renderer.setExposure(1.4f);
            renderer.setLighting(core::SceneLighting{});
            activeCinematic_ = buildClassShowcaseCinematic(beat.classType, beat.cameraOrigin);
            match_->selectClass(kTrailerPlayerIdA, beat.classType);
            spawnParticleBurst(beat.cameraOrigin, 25);
            std::fprintf(stdout, "[trailer] Scene 2: Class Showcase -- %s\n", tntwars::playerClassName(beat.classType));
            break;

        case BeatAction::TriggerUltimate: {
            renderer.setCinematicMode(false); // see Opening's own comment on why
            renderer.setExposure(1.4f);
            renderer.setLighting(core::SceneLighting{});
            activeCinematic_ = tntwars::buildUltimateCinematic(tntwars::ultimateForClass(beat.classType), beat.cameraOrigin);
            match_->selectClass(kTrailerPlayerIdA, beat.classType);
            tntwars::ClassStats stats = tntwars::classStatsFor(beat.classType);
            match_->ultimateCharge().addCharge(kTrailerPlayerIdA, stats.ultimateChargeRequired, stats.ultimateChargeRequired);
            tntwars::TntWarsMatch::UltimateResult result = match_->triggerUltimate(kTrailerPlayerIdA, elapsedSeconds_);
            spawnParticleBurst(beat.cameraOrigin, 50);
            std::fprintf(stdout, "[trailer] Scene 4: Ultimate -- %s (%s)\n", tntwars::playerClassName(beat.classType),
                         result.accepted ? "accepted" : "rejected");
            break;
        }

        case BeatAction::FinalClash:
            renderer.setCinematicMode(false); // see Opening's own comment on why
            renderer.setExposure(1.4f);
            renderer.setLighting(core::SceneLighting{});
            applyMap(beat.mapId, textureLibrary);
            addFinalClashLavaBackdrop(textureLibrary);
            activeCinematic_ = buildFinalClashCinematic(beat.cameraOrigin);
            match_->selectClass(kTrailerPlayerIdA, tntwars::PlayerClassType::Striker);
            match_->selectClass(kTrailerPlayerIdB, tntwars::PlayerClassType::Saboteur);
            nextFinalClashTriggerIndex_ = 0;
            lavaWasErupting_ = false;
            finalClashLava_ = tntwars::LavaEruptionState{};
            finalClashThruster_ = tntwars::ThrusterPlatformState{};
            std::fprintf(stdout, "[trailer] Scene 5: Final Clash\n");
            break;

        case BeatAction::ShowMiningSimZone:
            spawnMiningSimZoneScene(beat.zoneType, renderer, textureLibrary);
            activeCinematic_ = buildMiningSimZoneCinematic(beat.zoneType, miningsim::kMiningSimRtxSceneCenter());
            std::fprintf(stdout, "[trailer] Mining Sim RTX -- zone %s\n", miningsim::zoneTypeName(beat.zoneType));
            break;

        case BeatAction::ShowDungeon:
            spawnDungeonScene(beat.rarity, renderer, textureLibrary);
            activeCinematic_ = buildDungeonCinematic(miningsim::kMiningSimRtxSceneCenter() - glm::vec3(20.0f, 0.0f, 20.0f));
            std::fprintf(stdout, "[trailer] Procedural Dungeon -- rarity %s\n", miningsim::rarityTierName(beat.rarity));
            break;

        case BeatAction::ShowBoss:
            spawnBossScene(beat.rarity, renderer, textureLibrary);
            activeCinematic_ = buildBossCinematic(miningsim::kMiningSimRtxSceneCenter());
            std::fprintf(stdout, "[trailer] Boss -- rarity %s\n", miningsim::rarityTierName(beat.rarity));
            break;

        case BeatAction::ShowForge:
            spawnForgeScene(beat.tool, renderer, textureLibrary);
            activeCinematic_ = buildForgeCinematic(miningsim::kMiningSimRtxSceneCenter());
            std::fprintf(stdout, "[trailer] Forge -- %s\n", miningsim::miningToolName(beat.tool));
            break;

        case BeatAction::LogoReveal:
            spawnLogoRevealScene(renderer, textureLibrary);
            activeCinematic_ = buildLogoRevealCinematic(miningsim::kMiningSimRtxSceneCenter());
            std::fprintf(stdout, "[trailer] Kronos logo reveal\n");
            break;

        case BeatAction::TitleCard: {
            // See spawnMiningSimZoneScene()'s own comment for why -- this
            // beat also renders a custom, non-daylight SceneLighting via
            // the same AuxiliaryScene capture target.
            renderer.setCinematicMode(false);
            renderer.setExposure(1.4f);
            clearMapGeometry();
            hasSpawnedMap_ = false;
            clearSpecialSceneGeometry();
            // Real, explicit dark backdrop (not a carry-over from
            // whatever beat happened to run before this one) -- a real
            // floor keeps real geometry in frame so auto-exposure has
            // something real to meter against (see
            // spawnFeatureShotFloor()'s own header comment on why an
            // empty frame overexposes).
            core::EntityId floor = spawnFeatureShotFloor(beat.cameraOrigin);
            if (floor != core::kNullEntity) spawnedSpecialSceneEntities_.push_back(floor);
            core::SceneLighting lighting;
            lighting.intensity = 0.0f;
            lighting.ambient = glm::vec3(0.05f, 0.045f, 0.05f);
            lighting.ambientGround = glm::vec3(0.03f, 0.025f, 0.02f);
            lighting.fogColor = glm::vec3(0.05f, 0.045f, 0.05f);
            lighting.fogDensity = 0.012f;
            lighting.skyZenithColor = glm::vec3(0.02f, 0.02f, 0.025f);
            lighting.skyHorizonColor = glm::vec3(0.02f, 0.02f, 0.025f);
            lighting.pointLights.push_back({beat.cameraOrigin + glm::vec3(0.0f, 5.0f, 6.0f), 16.0f,
                                             glm::vec3(0.85f, 0.75f, 0.55f), 10.0f});
            renderer.setLighting(lighting);
            activeCinematic_ = buildTitleCardCinematic(beat.cameraOrigin);
            // Real stdout stand-in -- engine_runtime has no on-screen
            // text rendering at all, the same real, established boundary
            // core::Interactable.hpp's own UI-hint comment already
            // documents.
            std::fprintf(stdout, "[title card] Kronos\n[title card] Kronos : A timeless creation.\n");
            break;
        }
    }
}

void TrailerDirector::tickFinalClash(float currentLocalTime) {
    while (nextFinalClashTriggerIndex_ < finalClashSchedule_.size() &&
           finalClashSchedule_[nextFinalClashTriggerIndex_].localTimeSeconds <= currentLocalTime) {
        const FinalClashTrigger& trigger = finalClashSchedule_[nextFinalClashTriggerIndex_];
        switch (trigger.kind) {
            case FinalClashTriggerKind::RocketFire:
                // Real accept/reject result deliberately ignored here --
                // this beat's own visual (the particle burst below)
                // fires regardless, matching this class's own header
                // comment on why the trailer's real visuals aren't tied
                // 1:1 to real gameplay-state acceptance.
                (void)match_->fireWeapon(kTrailerPlayerIdA, activeLookAt_ + glm::vec3(0.0f, 1.0f, 10.0f),
                                          glm::vec3(0.0f, -0.1f, -1.0f), elapsedSeconds_);
                spawnParticleBurst(activeLookAt_, 20);
                break;
            case FinalClashTriggerKind::TorpedoFire:
                (void)match_->fireWeapon(kTrailerPlayerIdB, activeLookAt_ + glm::vec3(0.0f, -1.0f, 10.0f),
                                          glm::vec3(0.0f, 0.0f, -1.0f), elapsedSeconds_);
                spawnParticleBurst(activeLookAt_ + glm::vec3(0.0f, -2.0f, 0.0f), 20);
                break;
            case FinalClashTriggerKind::ThrusterHit:
                tntwars::applyThrusterHit(finalClashThruster_);
                spawnParticleBurst(activeLookAt_ + glm::vec3(5.0f, 0.0f, 0.0f), 15);
                break;
        }
        ++nextFinalClashTriggerIndex_;
    }
}

void TrailerDirector::tick(float dt, core::Renderer& renderer, core::TextureLibrary& textureLibrary) {
    float scaledDt = dt * playbackSpeed_;
    elapsedSeconds_ += scaledDt;

    TrailerTimeline::SceneQuery query = timeline_.queryAt(elapsedSeconds_);
    if (query.sceneIndex != lastSceneIndex_) {
        if (query.sceneIndex >= 0) dispatchBeatStart(query.sceneIndex, renderer, textureLibrary);
        lastSceneIndex_ = query.sceneIndex;
    }

    if (query.sceneIndex >= 0) {
        const TrailerBeat& beat = beats_[static_cast<size_t>(query.sceneIndex)];

        tntwars::CinematicSample sample = activeCinematic_.sample(query.localTimeSeconds, activeLookAt_);
        glm::vec3 shakeOffset = (beat.action == BeatAction::FinalClash) ? sampleShake(elapsedSeconds_) : glm::vec3(0.0f);
        camera_->position = sample.cameraPosition + shakeOffset;
        camera_->yawDegrees = sample.yawDegrees;
        camera_->pitchDegrees = sample.pitchDegrees;
        // Sprint 16 ("Cinematic Graphics" Phase 4, focal length changes)
        // -- real, only applied when the active cinematic actually
        // authored FOV keyframes (CinematicSample's own -1.0 "no
        // override" sentinel, see its header comment); every beat that
        // never calls addFovKeyframe() leaves the camera's own existing
        // FOV completely untouched, byte-for-byte the pre-Sprint-16
        // behavior.
        if (sample.fovDegrees > 0.0f) camera_->verticalFovDegrees = sample.fovDegrees;

        if (beat.action == BeatAction::FinalClash) {
            tickFinalClash(query.localTimeSeconds);

            tntwars::tickLavaEruption(finalClashLava_, scaledDt);
            bool erupting = finalClashLava_.isErupting();
            if (erupting && !lavaWasErupting_) {
                spawnParticleBurst(glm::vec3(20.0f, -5.0f, -20.0f), 40);
            }
            lavaWasErupting_ = erupting;
        }
    }

    if (capturing_ && captureInitialized_) {
        captureAccumulator_ += scaledDt;
        float captureInterval = outputFps_ > 0.0f ? 1.0f / outputFps_ : 0.0f;
        while (captureInterval > 0.0f && captureAccumulator_ >= captureInterval) {
            if (captureRig_.captureFrame(renderer, *ecs_, *meshLibrary_, textureLibrary, *camera_, outputDirectory_, framesSaved_)) {
                ++framesSaved_;
            }
            captureAccumulator_ -= captureInterval;
        }
    }
}

} // namespace engine::trailer
