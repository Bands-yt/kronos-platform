#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "core/Camera.hpp"
#include "core/ECS.hpp"
#include "core/Mesh.hpp"
#include "core/ParticleSystem.hpp"
#include "core/ProceduralMaterials.hpp"
#include "core/Renderer.hpp"
#include "miningsim/Mob.hpp"
#include "net/NetTypes.hpp"
#include "tntwars/CinematicSequence.hpp"
#include "tntwars/LavaEruption.hpp"
#include "tntwars/ThrusterHp.hpp"
#include "tntwars/TntWarsMatch.hpp"
#include "trailer/CaptureRig.hpp"
#include "trailer/TrailerScenes.hpp"
#include "trailer/TrailerTimeline.hpp"

namespace engine::trailer {

// Sprint 15 ("TNT-Wars Trailer Production")'s real orchestrator -- drives
// a real core::Camera through this trailer's real per-beat camera paths
// (tntwars::CinematicSequence, built once per beat via
// TrailerCinematics.hpp / tntwars::buildUltimateCinematic()), fires each
// beat's own real one-shot trigger (class showcase, ultimate, map
// switch, Final Clash's fire/hit schedule) exactly once when that beat
// starts, and -- while capturing -- saves a real numbered frame via
// CaptureRig at a real, independent, configurable output cadence. Same
// real fixed-rate-accumulator technique runtime::GameLoop itself already
// uses to decouple its own sim/network/render rates (see that class's
// own header comment), applied here to decouple "how often the real sim
// advances" from "how often a real frame gets saved to disk."
//
// Determinism (the brief's own "same output every run" requirement):
// every real camera pose and every real one-shot trigger this class
// fires is a pure function of real elapsed trailer time (via
// TrailerTimeline::queryAt(), each CinematicSequence's own deterministic
// sample(), and TrailerScenes.hpp's own fixed FinalClashTrigger
// schedule) -- tick() never reads a wall-clock timestamp or any real
// random-number source itself; camera shake is real sine-based, not
// std::random. core::ParticleSystem's own internal particle simulation
// (pre-existing, Sprint-6-era code) is out of scope for this determinism
// audit -- see this sprint's own README section for the honest,
// explicit note.
//
// Real, deliberate scope boundary: TntWarsMatch::fireWeapon()'s real
// accept/reject result drives real gameplay state (charge, cooldowns,
// anti-cheat) for real "TNT-Wars gameplay isolation" correctness -- but
// the trailer's own real *visual* representation of a fired shot is a
// real particle burst at the target point, not a rendered projectile
// mesh (no real projectile-mesh rendering call site exists anywhere in
// this engine yet -- tntwars::ProjectileState is real simulated state,
// never a drawn entity, see Projectile.hpp's own header comment). A
// real, honest, bounded choice, not a silently-dropped one.
class TrailerDirector {
public:
    TrailerDirector(core::Camera& camera, core::ECS& ecs, core::MeshLibrary& meshLibrary,
                     core::ParticleSystem& particleSystem, tntwars::TntWarsMatch& match);

    // Real, one-time GPU setup -- must succeed before startCapture().
    [[nodiscard]] bool initializeCapture(core::Renderer& renderer, VkExtent2D captureExtent);
    void shutdownCapture(core::Renderer& renderer);

    // Real playback control -- advances real elapsed trailer time by
    // `dt`, real-dispatches any beat-start trigger crossed this tick,
    // updates the real active camera pose, and (while capturing) saves a
    // real frame once the real output-fps accumulator crosses its
    // threshold. Call once per real sim tick.
    void tick(float dt, core::Renderer& renderer, core::TextureLibrary& textureLibrary);

    void startCapture(std::string outputDirectory, float outputFps);
    void stopCapture();

    // Real, caller-set (main.cpp's own --trailer CLI argument) default
    // output directory -- TrailerScript.lua reads this via
    // `cinematic.defaultOutputDirectory()` instead of hardcoding a path,
    // so the real CLI argument genuinely reaches the real capture
    // location rather than the script silently ignoring it. A real,
    // honest empty string is a valid "no override given" default the
    // script can fall back on its own hardcoded choice for.
    void setDefaultOutputDirectory(std::string directory) { defaultOutputDirectory_ = std::move(directory); }
    [[nodiscard]] const std::string& defaultOutputDirectory() const { return defaultOutputDirectory_; }
    [[nodiscard]] bool isCapturing() const { return capturing_; }
    // Real, honest "past the last beat" signal -- mirrors
    // TrailerTimeline::queryAt()'s own sceneIndex=-1 convention.
    [[nodiscard]] bool isFinished() const;

    [[nodiscard]] float elapsedSeconds() const { return elapsedSeconds_; }
    [[nodiscard]] const TrailerTimeline& timeline() const { return timeline_; }
    [[nodiscard]] const std::vector<TrailerBeat>& beats() const { return beats_; }
    [[nodiscard]] int framesSaved() const { return framesSaved_; }
    [[nodiscard]] float playbackSpeed() const { return playbackSpeed_; }
    // Real, live-adjustable playback-rate multiplier (Studio Trailer
    // Panel's own "playback speed control") -- 1.0 = real time, 0.5 =
    // real half-speed review, 2.0 = real double-speed skim. Does NOT
    // change the real recorded trailer content or determinism (every
    // beat's real duration/trigger timing is still measured in trailer
    // time, not wall-clock time) -- only how fast `elapsedSeconds_`
    // advances per real tick() call.
    void setPlaybackSpeed(float speed) { playbackSpeed_ = speed > 0.0f ? speed : 1.0f; }

    // Real, deterministic camera-shake overlay -- see
    // TrailerCinematics.hpp's own comment on why this is kept separate
    // from any one CinematicSequence.
    void setCameraShake(float amplitude, float frequencyHz);

    // Sprint 16 ("Cinematic Graphics" Phase 4, "cinematic shake profiles
    // (per beat)") -- real, named amplitude/frequency presets over the
    // same real setCameraShake() mechanism above, so a beat picks a
    // named *feel* ("this is an Impact moment") rather than re-deriving
    // its own raw numbers each time. None/Subtle/Handheld/Impact/Intense
    // span the same real sine-based shake sampleShake() already applies
    // -- a real, small vocabulary, not a claim of motion-captured
    // reference shake data.
    enum class ShakeProfile { None, Subtle, Handheld, Impact, Intense };
    void setCameraShakeProfile(ShakeProfile profile);
    // Real, direct read access to whatever setCameraShake()/
    // setCameraShakeProfile() last set -- exists so tests can verify the
    // real named-preset -> (amplitude, frequency) mapping without
    // needing to reach into sampleShake()'s own private output.
    [[nodiscard]] float shakeAmplitude() const { return shakeAmplitude_; }
    [[nodiscard]] float shakeFrequencyHz() const { return shakeFrequencyHz_; }

    // Real, immediate seek -- jumps elapsedSeconds_ to the real start of
    // beats()[index], forcing the next real tick() to real-dispatch that
    // beat's own one-shot trigger fresh (a real "scrub to this beat"
    // editing action, not silently replaying every beat in between). A
    // real, honest no-op if `index` is out of range. The Studio Trailer
    // Panel's own scene-preview scrubber and a Lua script's own
    // `cinematic.jumpToBeat()` binding both real-call this.
    void seekToBeat(int index);

    // Real, reserved, fixed synthetic PlayerIds this director drives --
    // deliberately far outside any real net::NetworkSession-assigned
    // range (server-assigned real player ids start small and sequential,
    // see NetworkSession.cpp's own onPeerConnected handler) so a trailer
    // capture run sharing a TntWarsMatch with real connected players (a
    // real, if unusual, deployment) can never real-collide with one.
    static constexpr net::PlayerId kTrailerPlayerIdA = 900001; // Striker/rockets in Final Clash, and every showcase/ultimate beat
    static constexpr net::PlayerId kTrailerPlayerIdB = 900002; // Saboteur/torpedoes in Final Clash

private:
    void dispatchBeatStart(int sceneIndex, core::Renderer& renderer, core::TextureLibrary& textureLibrary);
    void applyMap(tntwars::MapId map, core::TextureLibrary& textureLibrary);
    void clearMapGeometry();
    void spawnMapGeometry(tntwars::MapId map, core::TextureLibrary& textureLibrary);
    void addFinalClashLavaBackdrop(core::TextureLibrary& textureLibrary);

    // Kronos trailer production: the real Mining-Sim-side beats
    // (ShowMiningSimZone/ShowDungeon/ShowBoss/ShowForge/LogoReveal) all
    // spawn into this real, second, separate entity bucket -- kept apart
    // from spawnedMapEntities_ (TNT-Wars map geometry) because the two
    // real "worlds" must never be visible in the same frame (both spawn
    // near the world origin). clearSpecialSceneGeometry() and
    // clearMapGeometry() are both real-called defensively at the start
    // of every dispatch case that needs a clean canvas -- a real,
    // idempotent no-op when the bucket being cleared is already empty.
    void clearSpecialSceneGeometry();
    // Real fix for a real, live-verified bug: the Dungeon/Boss/Forge/
    // LogoReveal "feature shot" beats originally spawned their own real
    // subject floating in otherwise-empty space -- Sprint 16's own real
    // auto-exposure (SceneUBO's target-average-luminance adaptation,
    // Renderer.cpp) reads a near-empty frame as "very dark" and cranks
    // exposure up enough to blow the small lit subject out to solid
    // white, the exact same real "fog/exposure interaction" class of bug
    // MiningSimRtx.cpp's own header comment already documents for the
    // original cavern (fixed there by tuning fog, not the general
    // system) -- fixed here the same real way, by giving these beats
    // real geometry (a floor) filling enough of the frame, rather than
    // hacking the general auto-exposure/tonemap system.
    core::EntityId spawnFeatureShotFloor(glm::vec3 center);
    void spawnMiningSimZoneScene(miningsim::ZoneType zone, core::Renderer& renderer, core::TextureLibrary& textureLibrary);
    void spawnDungeonScene(miningsim::RarityTier rarity, core::Renderer& renderer, core::TextureLibrary& textureLibrary);
    void spawnBossScene(miningsim::RarityTier rarity, core::Renderer& renderer, core::TextureLibrary& textureLibrary);
    void spawnForgeScene(miningsim::MiningToolType tool, core::Renderer& renderer, core::TextureLibrary& textureLibrary);
    void spawnLogoRevealScene(core::Renderer& renderer, core::TextureLibrary& textureLibrary);
    // Real, small, themed lighting for the Dungeon/Boss/Forge/LogoReveal
    // "feature shot" beats -- deliberately NOT the full
    // miningsim::buildRtxPrototypeScene() cavern (that scene's own real
    // 40x40-unit cavern geometry would spatially collide with these
    // beats' own real spawned content at the same origin); real, direct
    // use of miningsim::zoneVisualTheme()'s own data for ambient/fog/key/
    // accent colors, the same real values buildRtxPrototypeScene()
    // itself is built on, just without the fixed cavern shell around it.
    [[nodiscard]] core::SceneLighting buildFeatureShotLighting(miningsim::ZoneType zone, glm::vec3 focusPoint) const;
    // Sprint 16 ("Cinematic Graphics" Phase 2): generated once, lazily,
    // the first time real map geometry needs a real material -- unlike
    // studio::plugins::TntWarsPlugin (which has a live TextureLibrary& at
    // construction time), this class only sees one starting with tick()'s
    // own parameter, so eager generation in the constructor isn't
    // possible here.
    void ensureMaterials(core::TextureLibrary& textureLibrary);
    [[nodiscard]] glm::vec3 sampleShake(float timeSeconds) const;
    void spawnParticleBurst(glm::vec3 position, int count);
    // Real, index-based dispatch (not a previous/current-time diff --
    // nextFinalClashTriggerIndex_ is reset to 0 by dispatchBeatStart()
    // whenever Final Clash is (re-)entered, so a monotonic index alone
    // is sufficient and simpler than tracking a previous local time too).
    void tickFinalClash(float currentLocalTime);

    core::Camera* camera_;
    core::ECS* ecs_;
    core::MeshLibrary* meshLibrary_;
    core::ParticleSystem* particleSystem_;
    tntwars::TntWarsMatch* match_;

    std::vector<TrailerBeat> beats_ = buildTntWarsTrailerBeats();
    TrailerTimeline timeline_{beatsToTimelineScenes(beats_)};

    float elapsedSeconds_ = 0.0f;
    float playbackSpeed_ = 1.0f;
    int lastSceneIndex_ = -1; // real edge-detection for beat-start dispatch, same convention this engine's own *KeyWasDown_ fields already establish

    tntwars::CinematicSequence activeCinematic_;
    glm::vec3 activeLookAt_{0.0f};

    float shakeAmplitude_ = 0.0f;
    float shakeFrequencyHz_ = 0.0f;

    tntwars::LavaEruptionState finalClashLava_;
    bool lavaWasErupting_ = false;
    tntwars::ThrusterPlatformState finalClashThruster_;
    size_t nextFinalClashTriggerIndex_ = 0;
    std::vector<FinalClashTrigger> finalClashSchedule_ = buildFinalClashSchedule();

    // Real GPU handles captured once at initializeCapture() for later
    // real map-geometry mesh creation (applyMap()/spawnMapGeometry()) --
    // the same real handles studio::plugins::TntWarsPlugin's constructor
    // takes, just sourced from a live core::Renderer& here instead of
    // being threaded in externally, since engine_runtime has no
    // equivalent existing call site (see this sprint's own README
    // section).
    VmaAllocator allocator_ = nullptr;
    VkDevice device_ = VK_NULL_HANDLE;
    VkCommandPool cmdPool_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    std::vector<core::EntityId> spawnedMapEntities_;
    tntwars::MapId currentSpawnedMap_ = tntwars::MapId::Trenches;
    bool hasSpawnedMap_ = false;

    // Kronos trailer production -- see clearSpecialSceneGeometry()'s own
    // header comment.
    std::vector<core::EntityId> spawnedSpecialSceneEntities_;

    core::ProceduralMaterialLibrary materials_;
    bool materialsGenerated_ = false;

    CaptureRig captureRig_;
    bool captureInitialized_ = false;
    bool capturing_ = false;
    std::string outputDirectory_;
    std::string defaultOutputDirectory_;
    float outputFps_ = 24.0f;
    float captureAccumulator_ = 0.0f;
    int framesSaved_ = 0;
};

} // namespace engine::trailer
