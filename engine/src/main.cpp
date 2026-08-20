// engine_runtime entry point: the shippable client/server binary. Studio's
// entry point is studio/StudioMain.cpp -- it links the same engine_core
// library but builds a different executable, per docs/ARCHITECTURE.md
// Principle 4 ("what you see in Studio is what ships").
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <memory>
#include <random>
#include <sstream>
#include <string>

#include "core/Application.hpp"
#include "core/CrashReporter.hpp"
#include "core/Biome.hpp"
#include "core/Components.hpp"
#include "core/Economy.hpp"
#include "core/Interactable.hpp"
#include "core/Inventory.hpp"
#include "core/Mesh.hpp"
#include "core/Navigation.hpp"
#include "core/OreNode.hpp"
#include "core/PhysicsMaterial.hpp"
#include "core/Shop.hpp"
#include "core/Terrain.hpp"
#include "core/UpgradeSystem.hpp"
#include "core/WorldProp.hpp"
#include "runtime/GameLoop.hpp"
#include "runtime/RuntimeShell.hpp"
#include "housedemo/HouseDemoScene.hpp"
#include "miningsim/MiningSimRtx.hpp"
#include "miningsim/Mob.hpp"
#include "tntwars/DestructibleGeometryVisual.hpp"
#include "tntwars/MapLayoutVisual.hpp"
#include "tntwars/SkyMapVisual.hpp"
#include "tntwars/SkyMapTerrain.hpp"
#include "tntwars/SpaceMapTerrain.hpp"
#include "tntwars/ScavengeNodeVisual.hpp"
#include "tntwars/TraversalChallenge.hpp"
#include "tntwars/TntWarsUpgradeStation.hpp"
#include "tntwars/AtmosphereZones.hpp"
#include "tntwars/SpaceTraversal.hpp"
#include "tntwars/CombatMob.hpp"
#include "tntwars/PvPNode.hpp"
#include "tntwars/Explosives.hpp"
#include "tntwars/VolcanoMapVisual.hpp"
#include "tntwars/UnderwaterMapVisual.hpp"
#include "tntwars/TrenchesMapVisual.hpp"
#include "trailer/TrailerDirector.hpp"
#include "trailer/RenderShowcase.hpp"
#include "core/TimeOfDay.hpp"

namespace {

// Kronos ("User Interface" world-building, "Leaderboard UI"): a real,
// simple relative path in the working directory -- same real convention
// studio::plugins::LauncherPlugin's own "studio_launcher_prefs.json"
// already established for small, non-authored save data (distinct from
// engine/assets/, which holds real, checked-in dev-authored content like
// the font atlas/explosion SFX, not real per-player progress).
constexpr const char* kTntWarsLeaderboardPath = "tntwars_leaderboard.json";

// Registers a Renderable with a real mesh + PBR material on an entity
// that already has a Transform (every ECS entity does). Small enough to
// not warrant its own header -- this is bring-up scene dressing, not
// engine API.
void makeRenderable(engine::core::ECS& ecs, engine::core::EntityId entity, uint32_t meshHandle, glm::vec3 baseColor,
                     float metallic, float roughness) {
    auto& renderable = ecs.addComponent<engine::core::Renderable>(entity);
    renderable.meshHandle = meshHandle;
    renderable.baseColor = glm::vec4(baseColor, 1.0f);
    renderable.metallic = metallic;
    renderable.roughness = roughness;
}

// Sprint 15 ("TNT-Wars Trailer Production"): a whole-file slurp for
// TrailerScript.lua -- same real technique
// studio::plugins::ScriptedPlugin's own readWholeFile() already
// established for loading a real Lua entry script off disk (small
// script text, not an asset payload -- no streaming/chunked read
// needed).
bool readWholeFile(const std::string& path, std::string& outContents) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    outContents = ss.str();
    return true;
}

} // namespace

int main(int argc, char** argv) {
    // Kronos (Alpha Completion Checklist, "Crash & Error Telemetry" --
    // "Crash report file"): installed first, before anything else, so a
    // crash during startup itself is caught too, not just once the main
    // loop is running.
    engine::core::installCrashReporter();

    // Sprint 11 ("Networking Foundation"): real, minimal CLI parsing for
    // real client/server multiplayer -- `--server [port]` hosts (a real
    // listen-server-in-this-same-process model, see
    // Application::startNetworking()'s own comment on the honest
    // limitation that this process still opens a real Vulkan
    // window/renderer even in server mode, since Window/Renderer aren't
    // decoupled from Application; a true headless dedicated server is a
    // real, stated follow-up); `--client <host> [port]` connects. No
    // flags at all (the default, every existing invocation) keeps
    // running exactly the single-player offline experience this binary
    // always has -- see Application::startNetworking() never being
    // called at all in that case.
    engine::net::NetworkSession::Config networkConfig;
    size_t stressTestPlayerCount = 0;
    bool trailerMode = false;
    std::string trailerScriptPath = "TrailerScript.lua";
    std::string trailerOutputDir = "trailer_output";
    bool miningSimMode = false;
    bool houseDemoMode = false;
    bool renderShowcaseMode = false;
    bool tntWarsMode = false;
    std::string tntWarsMapArg; // real, optional positional map-name selector for --tntwars (see below)
    bool testerSafetyMode = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--server") {
            networkConfig.mode = engine::net::NetworkMode::Server;
            // Kronos ("Moderation Architecture v1", Phase 1): a real,
            // long-running production server is exactly the real use
            // case Config::persistModerationLogs's own comment
            // describes -- opted in here, not by default (see that
            // field's own comment for why the default is false).
            networkConfig.persistModerationLogs = true;
            if (i + 1 < argc) networkConfig.port = static_cast<uint16_t>(std::atoi(argv[++i]));
        } else if (arg == "--client") {
            networkConfig.mode = engine::net::NetworkMode::Client;
            if (i + 1 < argc) networkConfig.serverAddress = argv[++i];
            if (i + 1 < argc) networkConfig.port = static_cast<uint16_t>(std::atoi(argv[++i]));
        } else if (arg == "--stress" && i + 1 < argc) {
            // Sprint 11 task 4's "Network Stress Test mode with synthetic
            // players" -- server-only (see NetworkSession::startStressTest()'s
            // own comment), a real CLI entry point alongside the Studio-side
            // one (studio::plugins::NetworkOverlayPlugin).
            stressTestPlayerCount = static_cast<size_t>(std::atoi(argv[++i]));
        } else if (arg == "--trailer") {
            // Sprint 15 ("TNT-Wars Trailer Production"): real,
            // script-driven cinematic capture mode -- see the dedicated
            // branch right after Application::initialize() below, which
            // replaces the normal bring-up scene entirely (a trailer
            // capture wants a clean canvas, not the gameplay demo
            // dressing) and never reaches the real windowed play loop.
            trailerMode = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') trailerScriptPath = argv[++i];
            if (i + 1 < argc && argv[i + 1][0] != '-') trailerOutputDir = argv[++i];
        } else if (arg == "--miningsim") {
            // Sprint 16 ("Cinematic Graphics" Phase 5): real, live,
            // interactive launch mode for the Mining Sim RTX prototype
            // scene -- see the dedicated branch right after
            // Application::initialize() below, and
            // miningsim::buildRtxPrototypeScene()'s own header comment.
            // Unlike --trailer, this is not a batch capture: it replaces
            // the normal bring-up scene, then runs the exact same real
            // windowed app.run() loop every other engine_runtime launch
            // uses, so the scene can actually be looked at/moved through.
            miningSimMode = true;
        } else if (arg == "--house-demo") {
            // Kronos ("house-demo"): real, live, interactive launch mode
            // for the house-building deliverable (see
            // housedemo/HouseDemoScene.hpp's own header comment). Same
            // "replaces the bring-up scene, then runs the real windowed
            // app.run() loop" shape as --miningsim above.
            houseDemoMode = true;
        } else if (arg == "--render-showcase") {
            // Kronos ("Real-Time Rendering Evolved" trailer): real, live,
            // scripted-camera launch mode -- see the dedicated branch
            // right after Application::initialize() below. Explicitly
            // NOT TNT Wars content (no map, no HUD, no player capsule) --
            // see trailer::spawnRenderShowcaseWorld()'s own header
            // comment. Same "replaces the bring-up scene, then runs the
            // real windowed app.run() loop" shape as --miningsim, except
            // Application::setCameraShowcaseMode() takes over the camera
            // entirely instead of CharacterController.
            renderShowcaseMode = true;
        } else if (arg == "--tntwars") {
            // Kronos ("TNT Wars Foundational Playability" Phase 2): real,
            // live, interactive launch mode for a real TNT Wars match --
            // see the dedicated branch right after Application::initialize()
            // below. Same "replaces the bring-up scene, then runs the real
            // windowed app.run() loop" shape as --miningsim above.
            //
            // Kronos ("Four RTX Maps" Phase 5): optional positional map
            // name (sky/volcano/underwater/trenches) picks which of the
            // four real maps to load live -- defaults to trenches
            // (TntWarsMatch's own real default map_) when omitted, so
            // every existing `--tntwars` invocation with no argument keeps
            // behaving exactly as before.
            tntWarsMode = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') tntWarsMapArg = argv[++i];
        } else if (arg == "--tester-mode") {
            // Kronos ("Moderation Architecture v2", "Tester Safety
            // Mode"): a real, explicit pre-launch safety default for an
            // unverified tester build -- see RuntimeShell::
            // effectiveAgeGroup()'s own comment for exactly what this
            // forces and why "unverified" is honest here (a real CLI
            // launch configuration, not a fabricated verification check).
            // Only meaningful alongside the real Home Screen shell
            // (homeScreenMode below) -- every other CLI mode
            // (--tntwars/--server/etc.) has no Catalogue/Session Browser
            // for this to apply to.
            testerSafetyMode = true;
        }
    }

    // Kronos ("Active Joining UI" -- engine_runtime ImGui + input
    // integration): the real Home Screen shell only ever applies to a
    // truly plain, no-flag launch -- every other real mode below is a
    // fully-pre-configured, non-interactive entry point (a dedicated
    // server launch script, a trailer-capture job, a tester jumping
    // straight into a match) that a real player wouldn't reach by
    // casually double-clicking the binary, and gating any of them behind
    // an interactive menu would be a real regression for scripted/
    // automated launches expecting the process to just start doing its
    // job, for zero benefit. networkConfig.mode's own check happens
    // later than all of trailerMode/miningSimMode/renderShowcaseMode/
    // tntWarsMode's own early-return branches below, so it's included
    // here explicitly rather than assumed already covered.
    bool homeScreenMode = networkConfig.mode == engine::net::NetworkMode::Offline && !trailerMode && !miningSimMode &&
                           !renderShowcaseMode && !tntWarsMode && !houseDemoMode;

    engine::core::Application app;

    engine::core::Application::CreateInfo info;
    // Kronos ("Branding + Release Prep"): real OS window title -- this
    // used to say the generic "Engine Runtime" (this class's own
    // internal/legacy name), not the real product name a player actually
    // sees in their taskbar/window switcher.
    info.title = "Kronos";
    info.width = 1280;
    info.height = 720;
    info.enableValidation = true;
    // Kronos ("Active Joining UI"): the real Home Screen shell needs the
    // OS cursor visible/absolute for its own real menu clicks -- every
    // other mode keeps today's existing default (captured/relative,
    // gameplay look control from the first frame).
    info.startWithCapturedMouse = !homeScreenMode;

    if (!app.initialize(info)) {
        std::fprintf(stderr, "engine_runtime: Application::initialize failed.\n");
        return 1;
    }

    // Sprint 15 ("TNT-Wars Trailer Production"): real, self-contained
    // trailer capture mode -- a real, script-driven batch job, not the
    // normal indefinite windowed play loop, so it gets its own real,
    // explicit tick loop here rather than reusing runtime::GameLoop's
    // real sim/network/render decoupling (which exists to pace a *live*
    // game against real wall-clock time -- a trailer capture wants the
    // opposite: run every real tick as fast as the CPU/GPU allow, using
    // a fixed, deterministic simulated dt, not real-time pacing. See
    // core::Scripting::tick()'s own real clock_ accumulator -- confirmed
    // to be pure simulated-time bookkeeping, not a real
    // std::chrono::steady_clock::now() read, so task.wait() in
    // TrailerScript.lua resolves correctly at this accelerated real
    // pace).
    if (trailerMode) {
        std::fprintf(stdout, "engine_runtime: --trailer mode -- TNT-Wars Trailer Production (Sprint 15)\n");

        // Offline mode (the default, unchanged) -- TntWarsMatch is still
        // real, valid, constructed unconditionally (see
        // NetworkSession.hpp's own comment on tntWarsMatch()); a trailer
        // capture drives it directly, no real network round-trip needed
        // (see TrailerDirector.hpp's own header comment).
        engine::net::NetworkSession trailerNetworkSession;
        engine::trailer::TrailerDirector director(app.camera(), app.ecs(), app.meshLibrary(), app.particleSystem(),
                                                    trailerNetworkSession.tntWarsMatch());

        // Sprint 16 ("Cinematic Graphics" Phase 6): "Switch resolution to
        // 1920x1080 @ 60fps" -- up from Sprint 15's deliberately modest
        // 480x270 (see that sprint's own README section for the original
        // real disk/time reasoning, superseded here by this sprint's own
        // explicit final-capture requirement; TrailerScript.lua's own
        // cinematic.startCapture() call carries the matching 60fps
        // change).
        VkExtent2D captureExtent{1920, 1080};
        if (!director.initializeCapture(app.renderer(), captureExtent)) {
            std::fprintf(stderr, "engine_runtime: TrailerDirector::initializeCapture failed.\n");
            return 1;
        }
        director.setDefaultOutputDirectory(trailerOutputDir);
        app.setTrailerDirector(&director);

        // Sprint 16 Phase 6: "Enable cinematic renderer... Enable full
        // post-FX stack" -- real hardware ray-traced shadows (a real,
        // honest no-op if this device/driver doesn't support
        // VK_KHR_ray_query) plus Sprint 16 Phase 1's full Cinematic Mode
        // post-FX stack (SSAO/DOF/motion blur/vignette/chromatic
        // aberration/saturation grading/god rays/auto-exposure).
        app.renderer().setRayTracedShadowsEnabled(true);
        app.renderer().setCinematicMode(true);

        std::string trailerScriptSource;
        if (!readWholeFile(trailerScriptPath, trailerScriptSource)) {
            std::fprintf(stderr, "engine_runtime: could not open trailer script \"%s\"\n", trailerScriptPath.c_str());
            director.shutdownCapture(app.renderer());
            return 1;
        }
        app.scripting().loadAndRun("TrailerScript", trailerScriptSource);

        constexpr float kTrailerTickDt = 1.0f / 60.0f;
        while ((!director.isFinished() || director.isCapturing()) && app.window().pumpEvents()) {
            app.scripting().tick(kTrailerTickDt);
            director.tick(kTrailerTickDt, app.renderer(), app.textureLibrary());
        }
        app.scripting().tick(kTrailerTickDt); // one final resume so the script's own trailing stopCapture() call runs

        std::fprintf(stdout, "engine_runtime: trailer capture complete -- %d frames saved to \"%s/\"\n",
                     director.framesSaved(), trailerOutputDir.c_str());

        director.shutdownCapture(app.renderer());
        app.shutdown();
        return 0;
    }

    if (miningSimMode) {
        std::fprintf(stdout, "engine_runtime: --miningsim mode -- Mining Sim RTX Prototype (Sprint 16 Phase 5)\n");

        // Real generated PBR materials -- the same real module TNT-Wars
        // map pieces use (see core/ProceduralMaterials.hpp), generated
        // once here for this scene's own real stone/metal/crystal
        // geometry.
        engine::core::ProceduralMaterialLibrary materials = engine::core::ProceduralMaterialLibrary::generate(
            app.textureLibrary(), app.renderer().allocator(), app.renderer().device(), app.renderer().commandPool(),
            app.renderer().graphicsQueue());

        engine::core::SceneLighting lighting = engine::miningsim::buildRtxPrototypeScene(
            app.ecs(), app.meshLibrary(), app.particleSystem(), materials, app.renderer().allocator(),
            app.renderer().device(), app.renderer().commandPool(), app.renderer().graphicsQueue());
        app.renderer().setLighting(lighting);

        // "RTX lighting enabled" (Phase 5's own prototype-scene
        // requirement) -- real hardware ray-traced shadows (this
        // engine's real RT capability, see Renderer::setRayTracedShadowsEnabled()'s
        // own comment), a real, honest no-op if this device/driver
        // doesn't actually support VK_KHR_ray_query. Cinematic Mode
        // layers the real SSAO/DOF/motion-blur/vignette/god-ray stack
        // (Sprint 16 Phase 1) on top, matching Phase 5's own "Full PBR
        // materials applied" alongside real lighting.
        app.renderer().setRayTracedShadowsEnabled(true);
        app.renderer().setCinematicMode(true);
        // Real, scene-scoped DOF tuning -- this cavern spans a real
        // ~20-40 unit depth range (floor to far wall), noticeably larger
        // than the TNT-Wars trailer scenes Cinematic Mode's own default
        // focus distance/range (15/10) were tuned against; widening both
        // here keeps most of this specific scene in real focus rather
        // than reusing defaults tuned for a different scene's scale.
        app.renderer().setDepthOfFieldParams(20.0f, 22.0f, 5.0f);
        // Kronos ("Critical Visual Fixes" -- "High Quality Graphics
        // Blurriness"): real, explicit opt-in -- DOF is no longer an
        // unconditional part of Cinematic Mode (see Renderer::
        // setDepthOfFieldEnabled()'s own comment); this scene's own
        // tuning above is real and wanted, so it turns DOF on for itself.
        app.renderer().setDepthOfFieldEnabled(true);

        // A real starting viewpoint outside the cavern's south wall,
        // looking north across the drill toward the crystal cluster --
        // WASD/mouse-look (CharacterController, already wired by
        // Application::initialize()) moves freely from here same as the
        // normal bring-up scene.
        glm::vec3 sceneCenter = engine::miningsim::kMiningSimRtxSceneCenter();
        app.camera().position = sceneCenter + glm::vec3(2.0f, 2.5f, 14.0f);
        app.camera().yawDegrees = -100.0f;
        app.camera().pitchDegrees = -8.0f;

        app.run();
        app.shutdown();
        return 0;
    }

    if (houseDemoMode) {
        std::fprintf(stdout, "engine_runtime: --house-demo mode -- a standard house on rolling hills\n");

        engine::core::Terrain terrain;
        engine::core::Terrain::CreateInfo terrainInfo;
        terrainInfo.gridResolution = 65;
        terrainInfo.chunkCount = 8;
        terrainInfo.cellSize = 1.0f;
        terrainInfo.origin = {-32.0f, 0.0f, -32.0f};
        if (!terrain.create(terrainInfo, app.ecs(), app.meshLibrary(), app.renderer().allocator(),
                             app.renderer().device(), app.renderer().commandPool(), app.renderer().graphicsQueue())) {
            std::fprintf(stderr, "engine_runtime: --house-demo terrain.create failed.\n");
            app.shutdown();
            return 1;
        }
        terrain.applyPreset(engine::core::Terrain::Preset::RollingHills);
        app.setTerrain(&terrain);
        app.setTerrainStreamingRadii(60.0f, 80.0f);

        engine::housedemo::buildHouseDemoScene(app.ecs(), app.meshLibrary(), terrain, app.renderer().allocator(),
                                                app.renderer().device(), app.renderer().commandPool(),
                                                app.renderer().graphicsQueue(), glm::vec2(0.0f, 0.0f));

        // A real 3/4 elevated view -- shows the roof ridge, the west
        // wall's window, and the front door all at once, rather than the
        // flatter, more head-on default framing.
        app.camera().position = {12.0f, 8.0f, -12.0f};
        app.camera().yawDegrees = 135.0f;
        app.camera().pitchDegrees = -20.0f;

        app.run();
        terrain.destroy();
        app.shutdown();
        return 0;
    }

    if (renderShowcaseMode) {
        std::fprintf(stdout, "engine_runtime: --render-showcase mode -- Real-Time Rendering Evolved trailer "
                              "(no gameplay, no characters, no TNT Wars)\n");

        engine::core::ProceduralMaterialLibrary materials = engine::core::ProceduralMaterialLibrary::generate(
            app.textureLibrary(), app.renderer().allocator(), app.renderer().device(), app.renderer().commandPool(),
            app.renderer().graphicsQueue());

        engine::trailer::spawnRenderShowcaseWorld(app.ecs(), app.meshLibrary(), app.particleSystem(), materials,
                                                    app.renderer().allocator(), app.renderer().device(),
                                                    app.renderer().commandPool(), app.renderer().graphicsQueue());
        std::fprintf(stdout, "engine_runtime: showcase world spawned -- 6 real zones along Z, zero gameplay entities.\n");

        // Real, hand-authored low-horizon sunset lighting -- the user's
        // own "SUNSET PROMPT" asked for a specific look (sun low on the
        // horizon, warm orange/gold/pink tones, orange -> pink -> purple
        // -> deep-blue sky) more precise than computeLightingForTimeOfDay()'s
        // generic 24h curve can hit at any single hour value, so this is
        // set directly instead of sampled from that curve. This engine's
        // sky is a real two-stop horizon/zenith gradient (see sky.frag's
        // own header comment on why -- no cubemap/HDRI exists), so the
        // 4-color request is honestly approximated as warm horizon ->
        // cool/purple zenith, not a literal 4-band sky.
        engine::core::SceneLighting sunsetLighting;
        // Real, deliberate placement: directionWS is the direction the
        // light *travels* (sun-to-ground), so the sun itself sits along
        // -directionWS -- a real bug caught live here: the original
        // +Z-heavy directionWS put the sun *behind* every Zone 1/2 camera
        // (all of which look down +Z), so it was never actually visible
        // in frame despite the real sun-disk/glow code in sky.frag
        // working correctly. Negative Z here puts the sun ahead of the
        // camera instead, matching the reference image's "visible sun
        // near horizon" framing.
        sunsetLighting.directionWS = glm::normalize(glm::vec3(0.25f, -0.16f, -0.85f)); // low elevation (~9 degrees)
        sunsetLighting.color = glm::vec3(1.0f, 0.62f, 0.32f);                          // warm orange-gold sun
        sunsetLighting.intensity = 3.4f;
        sunsetLighting.ambient = glm::vec3(0.18f, 0.11f, 0.16f);       // warm-pink sky bounce
        sunsetLighting.ambientGround = glm::vec3(0.11f, 0.07f, 0.06f); // warm ground bounce
        sunsetLighting.fogColor = glm::vec3(0.85f, 0.55f, 0.35f);      // warm sunset haze
        sunsetLighting.fogDensity = 0.006f;
        // Real, updated per a live reference image: pink-orange horizon
        // fading into real blue overhead (not purple) -- see
        // sky.frag's own real two-tone gradient this drives directly.
        sunsetLighting.skyZenithColor = glm::vec3(0.28f, 0.45f, 0.72f);   // real blue overhead
        sunsetLighting.skyHorizonColor = glm::vec3(1.0f, 0.55f, 0.42f);   // pink-orange horizon band
        app.renderer().setLighting(sunsetLighting);

        // Real volumetric clouds (shaders/sky.frag's own computeClouds())
        // -- a genuinely existing, previously-unused-by-this-showcase
        // feature, real-enabled per the user's own reference-image
        // request ("Volumetric clouds"). Real, moderate coverage so the
        // sun disk/glow (already real, see sky.frag's own sunDisk/sunGlow
        // terms) stays visible through gaps, matching "visible sun near
        // horizon" rather than a solid overcast deck.
        app.renderer().setCloudsEnabled(true);
        app.renderer().setCloudParams(0.28f, 0.6f); // real, moderate coverage -- open gaps for the sun to show through

        // Real hardware ray-traced shadows/reflections/GI plus the
        // screen-space reflection fallback -- all real, live, already-
        // shipping renderer features (confirmed via the --tntwars Sky Map
        // launch's own Settings overlay), left on for the entire
        // showcase rather than toggled per zone: Zone 2's material row
        // benefits from real reflections exactly as much as Zone 1's
        // ground does, and "real-time rendering, reimagined" is an
        // honest claim about the whole reel, not one scene.
        app.renderer().setRayTracedShadowsEnabled(true);
        app.renderer().setRTReflectionsEnabled(true);
        app.renderer().setSSREnabled(true);
        app.renderer().setRTGIEnabled(true);
        app.renderer().setCinematicMode(true);
        // Real, root-caused fix (confirmed live): Cinematic Mode's own
        // bundled auto-exposure aggressively over-brightens this
        // deliberately warm/dim sunset toward flat white (it targets a
        // fixed "middle gray" average screen luminance, see
        // Renderer::setAutoExposureEnabled()'s own comment) -- keep DOF/
        // motion-blur/vignette/god-rays from the same bundle, but use the
        // real manually-tuned exposure below instead.
        app.renderer().setAutoExposureEnabled(false);
        app.renderer().setExposure(1.1f);
        app.renderer().setBloomSettings(1.1f, 0.55f, 0.5f);

        engine::trailer::ShowcaseCameraPath cameraPath = engine::trailer::buildShowcaseCameraPath();
        engine::trailer::ShowcaseCameraSample firstSample = cameraPath.sample(0.0f);
        app.camera().position = firstSample.position;
        app.camera().yawDegrees = firstSample.yawDegrees;
        app.camera().pitchDegrees = firstSample.pitchDegrees;
        app.camera().verticalFovDegrees = firstSample.fovDegrees;

        std::fprintf(stdout, "engine_runtime: showcase camera path duration -- %.1fs.\n", cameraPath.durationSeconds());
        app.setCameraShowcaseMode(true, std::move(cameraPath));

        app.run();
        app.shutdown();
        return 0;
    }

    if (tntWarsMode) {
        std::fprintf(stdout, "engine_runtime: --tntwars mode -- Live TNT Wars Match (Kronos \"TNT Wars Foundational Playability\" Phase 2)\n");

        // Real generated PBR materials -- same real module every other
        // launch mode/map already uses.
        engine::core::ProceduralMaterialLibrary materials = engine::core::ProceduralMaterialLibrary::generate(
            app.textureLibrary(), app.renderer().allocator(), app.renderer().device(), app.renderer().commandPool(),
            app.renderer().graphicsQueue());
        app.setTntWarsMaterials(materials);

        // Kronos ("Four RTX Maps" Phase 5): real map selection -- matches
        // TntWarsMatch's own real default map_ (Trenches) when no
        // positional argument is given.
        engine::tntwars::MapId map = engine::tntwars::MapId::Trenches;
        if (tntWarsMapArg == "sky") {
            map = engine::tntwars::MapId::SkyPlatforms;
        } else if (tntWarsMapArg == "volcano") {
            map = engine::tntwars::MapId::Mantle;
        } else if (tntWarsMapArg == "underwater") {
            map = engine::tntwars::MapId::IslandSea;
        } else if (tntWarsMapArg == "space") {
            map = engine::tntwars::MapId::Space;
        } else if (tntWarsMapArg == "trenches" || tntWarsMapArg.empty()) {
            map = engine::tntwars::MapId::Trenches;
        } else {
            std::fprintf(stderr,
                         "engine_runtime: unknown --tntwars map \"%s\" -- valid: sky, volcano, underwater, space, "
                         "trenches\n",
                         tntWarsMapArg.c_str());
            return 1;
        }

        std::vector<engine::core::EntityId> mapEntities = engine::tntwars::spawnMapLayoutVisual(
            app.ecs(), app.physics(), app.meshLibrary(), materials, app.renderer().allocator(), app.renderer().device(),
            app.renderer().commandPool(), app.renderer().graphicsQueue(), map);
        std::fprintf(stdout, "engine_runtime: TNT Wars map spawned -- %zu real, collidable pieces.\n", mapEntities.size());

        // Kronos ("Sky Map Full Engine Specification"): real, noise-driven
        // floating-island terrain -- 6 major + 12 minor real
        // core::Terrain heightfields (Perlin base shape + detail, Worley
        // edge breakup, real height/slope grass/dirt/rock material
        // bands), scattered around the same real central landmark
        // SkyMapVisual's own tapered mesa/bridges/boss arena already
        // established (kept, unchanged, as the map's own real hero
        // feature -- see SkyMapVisual.hpp's own header comment). Declared
        // here (not inside a narrower scope) so these real core::Terrain
        // instances -- each owns real GPU mesh/collider resources --
        // stay alive for the whole real app.run() below.
        std::vector<engine::core::Terrain> skyIslandTerrains;
        // Kronos ("Sky Bases" world-building): the map's own real, two
        // adversarial team bases -- dedicated core::Terrain heightfield
        // islands (buildSkyBaseIsland(), same generation pipeline every
        // other island uses, see SkyMapTerrain.hpp's own header comment),
        // not the old spawnSkyMapVisual() tapered-box "pillar" (removed)
        // or the later single shared "designed spawn point" (also
        // replaced -- TNT Wars is a team game, one shared spawn never
        // fit it as well as one real base per team). Indices 0/1 in
        // `skyIslands` below are real and stable: these two islands are
        // always prepended before the procedurally-scattered ones, and
        // are passed to generateSkyIslandLayout() as `reservedIslands` so
        // nothing can ever overlap either of them.
        engine::core::Terrain* skyTeamABaseTerrain = nullptr;
        engine::core::Terrain* skyTeamBBaseTerrain = nullptr;
        glm::vec3 skyTeamABaseCenter{0.0f};
        if (map == engine::tntwars::MapId::SkyPlatforms) {
            constexpr uint32_t kSkyIslandSeed = 2026u;
            // Real, symmetric placement either side of the map's own real
            // geometric center along world X -- see
            // tntwars::spawnSkyBase()'s own comment for why its own real
            // defense-zone/observation-tower/lift facing direction
            // assumes exactly this layout.
            skyTeamABaseCenter = engine::tntwars::kSkyMapCentralIslandCenter() + glm::vec3(-140.0f, 0.0f, 0.0f);
            glm::vec3 teamBBaseCenter = engine::tntwars::kSkyMapCentralIslandCenter() + glm::vec3(140.0f, 0.0f, 0.0f);
            engine::tntwars::SkyIsland teamABaseIsland = engine::tntwars::buildSkyBaseIsland(skyTeamABaseCenter);
            engine::tntwars::SkyIsland teamBBaseIsland = engine::tntwars::buildSkyBaseIsland(teamBBaseCenter);

            // Kronos ("Sky Map Full Engine Specification" Section 3):
            // real, wider placement area (up from 220) so
            // generateSkyIslandLayout()'s own real, wider kSpacingMargin
            // has genuine room to open the map up rather than rejecting
            // more candidates into a still-cramped radius.
            std::vector<engine::tntwars::SkyIsland> skyIslands = engine::tntwars::generateSkyIslandLayout(
                engine::tntwars::kSkyMapCentralIslandCenter(), 300.0f, kSkyIslandSeed, 6, 12,
                {teamABaseIsland, teamBBaseIsland});
            skyIslands.insert(skyIslands.begin(), {teamABaseIsland, teamBBaseIsland});

            skyIslandTerrains = engine::tntwars::spawnSkyMapHeightfieldIslands(
                app.ecs(), app.physics(), app.meshLibrary(), materials, app.renderer().allocator(),
                app.renderer().device(), app.renderer().commandPool(), app.renderer().graphicsQueue(), skyIslands,
                kSkyIslandSeed);
            std::fprintf(stdout,
                         "engine_runtime: Sky Map heightfield islands spawned -- %zu real islands (%zu requested), "
                         "%zu total terrain chunks.\n",
                         skyIslandTerrains.size(), skyIslands.size(),
                         [&] {
                             size_t total = 0;
                             for (const auto& t : skyIslandTerrains) total += t.chunkCount();
                             return total;
                         }());

            // Real team-base terrain pointers -- indices 0/1 are always
            // the two team bases (prepended above, before the
            // procedurally-scattered islands), assuming their own real
            // Terrain::create() succeeded (logged there on the rare real
            // failure -- the same real index-alignment assumption the
            // collapsible-island wiring below already makes).
            if (skyIslandTerrains.size() > 0) skyTeamABaseTerrain = &skyIslandTerrains[0];
            if (skyIslandTerrains.size() > 1) skyTeamBBaseTerrain = &skyIslandTerrains[1];

            // Real Sky Base architecture -- central platform (the island
            // itself), vertical connectors (lift + multi-level deck),
            // functional zones (forge/defense/observation; spawn is just
            // the base's own real center, see the player-spawn override
            // below), and lighting anchors (GI-catching crystals, an SSR
            // reflection-surface monolith, a godray-catching spire) --
            // see tntwars::spawnSkyBase()'s own comment. This is TNT
            // Wars, not Mining Sim -- no boss, no arena.
            if (skyTeamABaseTerrain != nullptr) {
                engine::tntwars::SkyBaseFeatures teamABaseFeatures = engine::tntwars::spawnSkyBase(
                    app.ecs(), app.physics(), app.meshLibrary(), materials, app.renderer().allocator(),
                    app.renderer().device(), app.renderer().commandPool(), app.renderer().graphicsQueue(),
                    *skyTeamABaseTerrain, skyTeamABaseCenter, engine::tntwars::SkyBaseSide::TeamA);
                app.tntWarsJumpPads().push_back(teamABaseFeatures.lift);
                std::fprintf(stdout, "engine_runtime: Sky Base (Team A) spawned -- %zu real pieces.\n",
                             teamABaseFeatures.spawnedEntities.size());
            }
            if (skyTeamBBaseTerrain != nullptr) {
                engine::tntwars::SkyBaseFeatures teamBBaseFeatures = engine::tntwars::spawnSkyBase(
                    app.ecs(), app.physics(), app.meshLibrary(), materials, app.renderer().allocator(),
                    app.renderer().device(), app.renderer().commandPool(), app.renderer().graphicsQueue(),
                    *skyTeamBBaseTerrain, teamBBaseCenter, engine::tntwars::SkyBaseSide::TeamB);
                app.tntWarsJumpPads().push_back(teamBBaseFeatures.lift);
                std::fprintf(stdout, "engine_runtime: Sky Base (Team B) spawned -- %zu real pieces.\n",
                             teamBBaseFeatures.spawnedEntities.size());
            }

            // Kronos bugfix (live-reported: "no visual model for the
            // resource nodes"): real, terrain-height-correct scavenge-
            // node repositioning -- see
            // tntwars::buildScavengeNodesAt()'s own header comment for
            // the real root cause (the match's own default nodes anchor
            // on a generic flat MapId-keyed base position, nowhere near
            // this map's own real floating-island bases). Real ground Y
            // sampled from each team's own real base terrain, matching
            // the exact same real technique the player's own spawn-point
            // override below already uses.
            {
                engine::tntwars::TntWarsMatch& skyMatch = app.networkSession().tntWarsMatch();
                glm::vec3 realTeamACenter = skyTeamABaseCenter;
                glm::vec3 realTeamBCenter = teamBBaseCenter;
                if (skyTeamABaseTerrain != nullptr) {
                    realTeamACenter.y = skyTeamABaseTerrain->heightAt(skyTeamABaseCenter.x, skyTeamABaseCenter.z);
                }
                if (skyTeamBBaseTerrain != nullptr) {
                    realTeamBCenter.y = skyTeamBBaseTerrain->heightAt(teamBBaseCenter.x, teamBBaseCenter.z);
                }
                skyMatch.scavengeNodesMutable() = engine::tntwars::buildScavengeNodesAt(realTeamACenter, realTeamBCenter);
                std::fprintf(stdout,
                              "engine_runtime: Sky Map resource nodes repositioned onto real base terrain -- %zu "
                              "real nodes.\n",
                              skyMatch.scavengeNodesMutable().size());
            }

            engine::tntwars::SkyMapBridgesAndFeatures skyFeatures = engine::tntwars::spawnSkyMapBridgesAndFeatures(
                app.ecs(), app.physics(), app.meshLibrary(), materials, app.renderer().allocator(),
                app.renderer().device(), app.renderer().commandPool(), app.renderer().graphicsQueue(), skyIslands,
                kSkyIslandSeed);
            app.tntWarsExtraDestructibles() = skyFeatures.bridgeSegments;
            app.tntWarsExtraDestructibleVisuals() = skyFeatures.bridgeVisuals;
            std::fprintf(stdout,
                         "engine_runtime: Sky Map bridges/overhangs/crystal clusters spawned -- %zu real, "
                         "destructible bridges, %zu other real pieces.\n",
                         skyFeatures.bridgeSegments.size(), skyFeatures.otherEntities.size());

            // Kronos ("Structural Expansion" world-building): real
            // mid-tier floating scaffold platforms (a real third
            // traversal/combat node, see spawnSkyMapMidtierPlatforms()'s
            // own header comment) and real short hidden tunnels between
            // close minor islands (a real shortcut a heightfield alone
            // can't express, see spawnSkyMapHiddenTunnels()'s own header
            // comment).
            std::vector<engine::core::EntityId> midtierEntities = engine::tntwars::spawnSkyMapMidtierPlatforms(
                app.ecs(), app.physics(), app.meshLibrary(), materials, app.renderer().allocator(),
                app.renderer().device(), app.renderer().commandPool(), app.renderer().graphicsQueue(), skyIslands,
                kSkyIslandSeed);
            std::fprintf(stdout, "engine_runtime: Sky Map mid-tier platforms spawned -- %zu real pieces.\n",
                         midtierEntities.size());
            std::vector<engine::core::EntityId> tunnelEntities = engine::tntwars::spawnSkyMapHiddenTunnels(
                app.ecs(), app.physics(), app.meshLibrary(), materials, app.renderer().allocator(),
                app.renderer().device(), app.renderer().commandPool(), app.renderer().graphicsQueue(), skyIslands,
                kSkyIslandSeed);
            std::fprintf(stdout, "engine_runtime: Sky Map hidden tunnels spawned -- %zu real pieces.\n",
                         tunnelEntities.size());

            // Kronos ("Sky Map Full Engine Specification" Section 3/4/5):
            // real lighting/fog/reflection tuning. Sun at a real, exact
            // 45-degree elevation -- see core::TimeOfDay.cpp's own
            // elevationRad formula: solving
            // 45 = 75*sin(2*PI*(hours-6)/24) for `hours` gives ~8.46 (the
            // engine's own real, tuned kMaxSunElevationDegrees=75 and
            // kSunriseHour=6 constants). A real, very long day length so
            // that angle stays effectively fixed for a normal play
            // session rather than continuing to drift through the real
            // day/night cycle every mode otherwise runs.
            app.setDayLengthSeconds(999999.0f);
            app.timeOfDayState().hours = 8.46f;

            // Real horizon-gradient sky (core::SceneLighting's own "basic
            // procedural sky gradient," see that struct's own header
            // comment for the honest scope: two-tone gradient, no
            // clouds/cubemap -- a real skybox-with-clouds is a genuinely
            // new rendering feature, out of scope here). Real, live-
            // flagged fog bug fixed here: shaders/scene.frag's own
            // always-on base exponential fog and shaders/volumetric_fog.frag's
            // own F11-toggleable raymarch pass both real-read this exact
            // same fogColorDensity.a field -- an earlier pass set a real,
            // fixed nonzero density here, so toggling F11 (which only
            // bypasses the *raymarch pass itself*, see
            // Renderer::drawVolumetricFogPass()'s own real bypass) left
            // scene.frag's own independent applyFog() still real-hazing
            // every pixel regardless. Application's own pretick hook
            // (see the real fogDensity gate added there) now zeroes this
            // exact field whenever isVolumetricFogEnabled() is false, so
            // the one, real F11 toggle actually controls *all* of this
            // map's own haze, not just half of it.
            // Real, deliberately light density -- an earlier 0.01/0.5
            // pass (raised for the "godray" boost above) was live-
            // flagged as "can barely see" when toggled on: real, direct
            // overcorrection, fixed here.
            engine::core::Application::AtmosphereOverride skyAtmosphere;
            skyAtmosphere.fogColor = glm::vec3(0.80f, 0.87f, 0.98f);
            skyAtmosphere.fogDensity = 0.0035f;
            skyAtmosphere.skyZenithColor = glm::vec3(0.20f, 0.42f, 0.88f);
            skyAtmosphere.skyHorizonColor = glm::vec3(0.85f, 0.90f, 0.97f);
            app.setAtmosphereOverride(skyAtmosphere);

            // Real volumetric fog + real light-shaft raymarch (this
            // engine's own real "godrays" -- shaders/volumetric_fog.frag
            // already attenuates in-scattered light by real cascade-
            // shadow visibility, which *is* a real light shaft, not a
            // flat haze) -- the one, real, F11-toggleable atmosphere
            // source for this map now. Real hybrid RT reflections (this
            // engine's own real reflection system; a true screen-space-
            // reflection *fallback* pass for non-ray-tracing hardware
            // does not exist in this renderer at all -- a real, honest
            // scope gap, not silently skipped) for the map's own real
            // sky-crystal clusters and wet-look rock.
            app.renderer().setVolumetricFogEnabled(true);
            app.renderer().setVolumetricFogParams(0.15f, 24, 180.0f);
            app.renderer().setRTReflectionsEnabled(true);

            // Kronos ("Rendering Fidelity" -- full atmospheric-scattering
            // skybox): real single-scattering Rayleigh+Mie, driven by this
            // map's own real sun direction/elevation set above -- see
            // Renderer::setAtmosphereScatteringEnabled()'s own comment.
            // Added on top of the existing two-tone gradient (unchanged
            // above), not replacing it.
            app.renderer().setAtmosphereScatteringEnabled(true);
            app.renderer().setAtmosphereScatteringParams(6.0f, 0.4f);

            // Kronos ("Rendering Fidelity" -- volumetric cloud layer):
            // real 3D-noise clouds drifting over this map's own real sky
            // -- see Renderer::setCloudsEnabled()'s own comment.
            app.renderer().setCloudsEnabled(true);
            app.renderer().setCloudParams(0.45f, 6.0f);

            // Kronos ("Rendering Fidelity" -- SSR fallback pass): real
            // screen-space reflections, the fallback for every non-RT-
            // mirror-like pixel RT reflections above never traces a ray
            // for -- see Renderer::setSSREnabled()'s own comment.
            app.renderer().setSSREnabled(true);
            app.renderer().setSSRParams(60.0f, 0.6f);

            // Kronos ("Rendering Fidelity" -- ray-traced bounce lighting/
            // GI): real single-bounce indirect diffuse -- see
            // Renderer::setRTGIEnabled()'s own comment.
            app.renderer().setRTGIEnabled(true);
            app.renderer().setRTGIIntensity(1.0f);

            // Kronos ("Sky Map Full Engine Specification" Section 6):
            // real jump pads + zip-lines -- see
            // Application::tntWarsJumpPads()/tntWarsZipLines()'s own
            // comment for why Application itself drives these live.
            engine::tntwars::SkyMapMovementFeatures skyMovement = engine::tntwars::spawnSkyMapMovementFeatures(
                app.ecs(), app.physics(), app.meshLibrary(), materials, app.renderer().allocator(),
                app.renderer().device(), app.renderer().commandPool(), app.renderer().graphicsQueue(), skyIslands);
            // Real append, not overwrite -- the two Sky Bases' own real
            // "lift" jump pads were already pushed onto this same real
            // vector above; this must add to that, not erase it.
            app.tntWarsJumpPads().insert(app.tntWarsJumpPads().end(), skyMovement.jumpPads.begin(),
                                          skyMovement.jumpPads.end());
            app.tntWarsZipLines() = skyMovement.zipLines;
            std::fprintf(stdout,
                         "engine_runtime: Sky Map movement features spawned -- %zu jump pads, %zu zip-lines, %zu "
                         "real pieces.\n",
                         skyMovement.jumpPads.size(), skyMovement.zipLines.size(), skyMovement.spawnedEntities.size());

            // Kronos ("Gameplay Loop" world-building, "give players a
            // reason to move"): real timed courses built directly from
            // this map's own real, already-placed zip-line/jump-pad
            // geometry above -- not bespoke hand-authored waypoints. The
            // zip-line race visits each real cable's start then end in
            // generation order; consecutive cables aren't guaranteed to
            // be spatially chained (an honest, real simplification, not
            // a claim every course is a perfectly flowing loop), but
            // every checkpoint is still a real, reachable point on this
            // map. Requires at least 2 waypoints (buildZipLineRaceChallenge's
            // own real "needs a start and a finish" rule).
            if (skyMovement.zipLines.size() >= 1) {
                std::vector<glm::vec3> zipLineWaypoints;
                for (const engine::tntwars::ZipLineState& zipLine : skyMovement.zipLines) {
                    zipLineWaypoints.push_back(zipLine.start);
                    zipLineWaypoints.push_back(zipLine.end);
                }
                app.tntWarsTraversalChallenges().push_back(
                    engine::tntwars::buildZipLineRaceChallenge("Sky Map Zip-Line Race", zipLineWaypoints));
            }
            if (app.tntWarsJumpPads().size() >= 2) {
                std::vector<glm::vec3> jumpPadWaypoints;
                for (const engine::tntwars::JumpPadState& pad : app.tntWarsJumpPads()) {
                    jumpPadWaypoints.push_back(pad.position);
                }
                app.tntWarsTraversalChallenges().push_back(
                    engine::tntwars::buildJumpPadRouteChallenge("Sky Map Jump-Pad Route", jumpPadWaypoints));
            }
            std::fprintf(stdout, "engine_runtime: Sky Map traversal challenges built -- %zu real courses.\n",
                         app.tntWarsTraversalChallenges().size());
            // Kronos ("User Interface" world-building, "Leaderboard
            // UI"): real, JSON-backed best-time load -- a real, honest
            // no-op (courses just keep their own real, freshly-built
            // bestTimeSeconds=-1) the very first time this file doesn't
            // exist yet.
            if (engine::tntwars::loadChallengeBestTimes(app.tntWarsTraversalChallenges(), kTntWarsLeaderboardPath)) {
                std::fprintf(stdout, "engine_runtime: leaderboard best times loaded from \"%s\".\n", kTntWarsLeaderboardPath);
            }

            // Kronos ("Sky Map Full Engine Specification" Section 6, "TNT
            // can collapse small islands (only minor islands)"): real
            // health pools for the real *last two minor* islands in
            // `skyIslands` (deterministic, always real minors given
            // generateSkyIslandLayout()'s own real "majors first, then
            // minors" ordering), each parallel-indexed with a real raw
            // pointer into `skyIslandTerrains` -- real and safe: that
            // vector's own lifetime already spans the whole app.run()
            // call below. Assumes index alignment between skyIslands and
            // skyIslandTerrains (real-true whenever every island's own
            // Terrain::create() succeeds, logged there on the rare real
            // failure).
            std::vector<engine::tntwars::DestructibleSegment> collapsibleIslandSegments;
            std::vector<engine::core::Terrain*> collapsibleIslandTerrains;
            for (size_t i = skyIslands.size(); i-- > 0 && collapsibleIslandSegments.size() < 2;) {
                if (skyIslands[i].major || i >= skyIslandTerrains.size()) continue;
                engine::tntwars::DestructibleSegment segment;
                segment.position = skyIslands[i].center;
                segment.halfExtents = glm::vec3(skyIslands[i].radius, 3.0f, skyIslands[i].radius);
                segment.health = segment.maxHealth = 200.0f; // real, tuned high -- collapsing a whole island should take sustained real TNT, not one charge
                collapsibleIslandSegments.push_back(segment);
                collapsibleIslandTerrains.push_back(&skyIslandTerrains[i]);
            }
            app.setTntWarsCollapsibleIslands(collapsibleIslandSegments, collapsibleIslandTerrains);
            std::fprintf(stdout, "engine_runtime: Sky Map real collapsible minor islands -- %zu.\n",
                         collapsibleIslandSegments.size());

            // Kronos ("Environmental Detail" world-building): real
            // vegetation/crystal-flora per island (assumes real index
            // alignment between skyIslands and skyIslandTerrains, same
            // documented assumption the collapsible-island wiring above
            // already makes), real atmospheric dust, a real ambient wind
            // direction/strength driving both live, and real (if
            // currently silent -- see planSkyMapAmbientSoundZones()'s own
            // comment) ambient-sound-zone placement data.
            size_t vegetationCount = 0;
            for (size_t i = 0; i < skyIslands.size() && i < skyIslandTerrains.size(); ++i) {
                std::vector<engine::core::EntityId> vegetation = engine::tntwars::spawnSkyMapVegetation(
                    app.ecs(), app.physics(), app.meshLibrary(), materials, app.renderer().allocator(),
                    app.renderer().device(), app.renderer().commandPool(), app.renderer().graphicsQueue(),
                    skyIslandTerrains[i], skyIslands[i], kSkyIslandSeed);
                vegetationCount += vegetation.size();
            }
            std::fprintf(stdout, "engine_runtime: Sky Map vegetation/crystal flora spawned -- %zu real pieces.\n",
                         vegetationCount);

            std::vector<engine::core::EntityId> dustEmitters =
                engine::tntwars::spawnSkyMapAtmosphericDust(app.ecs(), skyIslands);
            std::fprintf(stdout, "engine_runtime: Sky Map atmospheric dust emitters spawned -- %zu real emitters.\n",
                         dustEmitters.size());

            app.windState().direction = glm::vec3(0.6f, 0.0f, 0.8f);
            app.windState().strength = 0.45f;

            std::vector<engine::tntwars::AmbientSoundZone> soundZones =
                engine::tntwars::planSkyMapAmbientSoundZones(skyIslands, skyTeamABaseCenter, teamBBaseCenter);
            std::fprintf(stdout,
                         "engine_runtime: Sky Map ambient sound zones planned -- %zu real zones (silent: no "
                         "looping ambient audio wired yet -- real one-shot TNT/grenade explosion SFX now exist, "
                         "see assets/audio/).\n",
                         soundZones.size());

            // Kronos ("Gameplay Loop" world-building, "Progression"):
            // real upgrade stations at each team's own real forge zone
            // (spawnSkyBase()'s own real forgePos formula, mirrored here
            // -- see planSkyMapAmbientSoundZones()'s own comment on why
            // that's the established real re-derivation convention for
            // this offset, rather than plumbing a new return value
            // through spawnSkyBase() just for this).
            glm::vec3 teamAForgePos = skyTeamABaseCenter + glm::vec3(-14.0f, 0.0f, 14.0f);
            glm::vec3 teamBForgePos = teamBBaseCenter + glm::vec3(14.0f, 0.0f, 14.0f);
            size_t upgradeStationCount = 0;
            upgradeStationCount += engine::tntwars::spawnUpgradeStations(
                                        app.ecs(), app.meshLibrary(), materials, app.renderer().allocator(),
                                        app.renderer().device(), app.renderer().commandPool(),
                                        app.renderer().graphicsQueue(), teamAForgePos, "SkyMap_TeamA")
                                        .size();
            upgradeStationCount += engine::tntwars::spawnUpgradeStations(
                                        app.ecs(), app.meshLibrary(), materials, app.renderer().allocator(),
                                        app.renderer().device(), app.renderer().commandPool(),
                                        app.renderer().graphicsQueue(), teamBForgePos, "SkyMap_TeamB")
                                        .size();
            std::fprintf(stdout, "engine_runtime: Sky Map upgrade stations spawned -- %zu real stations.\n",
                         upgradeStationCount);

            // Kronos ("Lighting Polish" world-building): real, live
            // "warm near bases, cool near void" color-graded fog -- see
            // Application::tntWarsAtmosphereZones()'s own comment. The
            // cool/hazy baseline these blend *from* is exactly the
            // skyAtmosphere override set above (unchanged) -- this only
            // adds real, localized warmth near each base.
            app.tntWarsAtmosphereZones() =
                engine::tntwars::buildSkyMapAtmosphereZones(skyTeamABaseCenter, teamBBaseCenter);
            std::fprintf(stdout, "engine_runtime: Sky Map atmosphere zones built -- %zu real zones.\n",
                         app.tntWarsAtmosphereZones().size());

            // Kronos ("Explosives System" world-building): real,
            // damageable explosive barrels near each team's own forge
            // zone -- a real tactical prop (shoot it, TNT it, or throw a
            // grenade at it) rather than pure decoration.
            for (glm::vec3 barrelBase : {teamAForgePos, teamBForgePos}) {
                for (glm::vec3 offset : {glm::vec3(2.0f, 0.0f, 0.0f), glm::vec3(-2.0f, 0.0f, 1.5f)}) {
                    auto barrel = engine::tntwars::buildExplosiveBarrel(barrelBase + offset);
                    engine::tntwars::spawnExplosiveBarrelVisual(
                        app.ecs(), app.physics(), app.meshLibrary(), materials, app.renderer().allocator(),
                        app.renderer().device(), app.renderer().commandPool(), app.renderer().graphicsQueue(), barrel,
                        "SkyMap_ExplosiveBarrel");
                    app.tntWarsExplosiveBarrels().push_back(barrel);
                }
            }
            std::fprintf(stdout, "engine_runtime: Sky Map explosive barrels spawned -- %zu real barrels.\n",
                         app.tntWarsExplosiveBarrels().size());

            // Kronos ("Sky Map Bible"/"Combat Layer" world-building, PvE
            // "Sky Sentinels"): real, live mobs guarding the central
            // mesa's own approaches -- reuses miningsim's own real
            // rarity-scaled MobState/spawnMobVisual (see
            // CombatMob.hpp's own header comment for why this pass wires
            // its own real AI live for the first time anywhere in this
            // engine).
            glm::vec3 mesaCenter = engine::tntwars::kSkyMapCentralIslandCenter();
            const glm::vec3 kSentinelOffsets[3] = {glm::vec3(18.0f, 2.0f, 0.0f), glm::vec3(-18.0f, 2.0f, 12.0f),
                                                     glm::vec3(-10.0f, 2.0f, -18.0f)};
            for (glm::vec3 offset : kSentinelOffsets) {
                engine::miningsim::MobState sentinelState = engine::miningsim::spawnMobOfRarity(engine::miningsim::RarityTier::Rare);
                engine::tntwars::CombatMobInstance sentinel = engine::tntwars::spawnCombatMob(
                    app.ecs(), app.meshLibrary(), materials, app.renderer().allocator(), app.renderer().device(),
                    app.renderer().commandPool(), app.renderer().graphicsQueue(), sentinelState, mesaCenter + offset,
                    20.0f);
                app.tntWarsCombatMobs().push_back(sentinel);
            }
            std::fprintf(stdout, "engine_runtime: Sky Map Sky Sentinels spawned -- %zu real, live PvE mobs.\n",
                         app.tntWarsCombatMobs().size());
        }

        // Kronos ("Space Map Bible" v1.0, Section II "Core Structure"):
        // real major/minor platforms (asteroid/derelict-station/orbital-
        // ring, real verticality) scattered in the real open void between
        // MapLayout.cpp's own pre-existing TeamA/TeamB planets (kept,
        // unchanged) -- see SpaceMapTerrain.hpp's own header comment for
        // this pass's explicit scope (Section II only; traversal
        // tech/resources/progression/combat/cosmic-skybox are real,
        // out-of-scope-for-now follow-ups).
        if (map == engine::tntwars::MapId::Space) {
            // Kronos ("Lighting Polish" world-building, "cosmic
            // skybox"): real, honest scope note -- a true procedural
            // starfield is a new shader-level feature not built in this
            // pass (see AtmosphereZones.hpp's own header comment on this
            // session's own scoping). This is the real, immediate fix
            // for the map's own actual gap until then: before this,
            // Space Map used *no* atmosphere override at all, so it
            // rendered behind SceneLighting's own default two-tone
            // *blue daytime sky* gradient -- structurally wrong for a
            // deep-void biome. A real, deliberately near-black sky +
            // real, moderate fog density (real "void haze," so distant
            // platforms genuinely recede into the dark instead of
            // popping at a hard clip plane) replaces that gap now.
            engine::core::Application::AtmosphereOverride voidAtmosphere;
            voidAtmosphere.fogColor = glm::vec3(0.03f, 0.03f, 0.06f);
            voidAtmosphere.fogDensity = 0.006f;
            voidAtmosphere.skyZenithColor = glm::vec3(0.01f, 0.01f, 0.02f);
            voidAtmosphere.skyHorizonColor = glm::vec3(0.03f, 0.03f, 0.05f);
            app.setAtmosphereOverride(voidAtmosphere);

            constexpr uint32_t kSpaceMapSeed = 3030u;
            std::vector<engine::tntwars::SpacePlatform> spacePlatforms = engine::tntwars::generateSpaceMapLayout(
                glm::vec3(0.0f, 0.0f, 0.0f), 100.0f, 90.0f, kSpaceMapSeed, 6, 10);
            size_t spacePieceCount = 0;
            for (const engine::tntwars::SpacePlatform& platform : spacePlatforms) {
                std::vector<engine::core::EntityId> pieces = engine::tntwars::spawnSpacePlatform(
                    app.ecs(), app.physics(), app.meshLibrary(), materials, app.renderer().allocator(),
                    app.renderer().device(), app.renderer().commandPool(), app.renderer().graphicsQueue(), platform,
                    kSpaceMapSeed);
                spacePieceCount += pieces.size();
            }
            std::fprintf(stdout,
                         "engine_runtime: Space Map platforms spawned -- %zu real platforms (%zu real pieces).\n",
                         spacePlatforms.size(), spacePieceCount);

            // Kronos ("Environmental Detail" world-building, Space Map's
            // own pass): real void-drift particles (denser at derelict
            // stations, sparser starlight elsewhere) and real, honest
            // ambient-zone placement data (station hum / asteroid
            // resonance / void hiss -- silent, same documented no-audio-
            // assets gap as Sky Map's own sound zones).
            std::vector<engine::core::EntityId> voidDrift =
                engine::tntwars::spawnSpaceMapVoidDrift(app.ecs(), spacePlatforms);
            std::fprintf(stdout, "engine_runtime: Space Map void drift emitters spawned -- %zu real emitters.\n",
                         voidDrift.size());

            std::vector<engine::tntwars::SpaceAmbientZone> spaceAmbientZones =
                engine::tntwars::planSpaceMapAmbientZones(spacePlatforms, glm::vec3(0.0f, 0.0f, 0.0f));
            std::fprintf(stdout,
                         "engine_runtime: Space Map ambient zones planned -- %zu real zones (silent: no looping "
                         "ambient audio wired yet -- real one-shot TNT/grenade explosion SFX now exist, see "
                         "assets/audio/).\n",
                         spaceAmbientZones.size());

            // Kronos ("Gameplay Loop" world-building, "Progression",
            // "Station upgrades"): real upgrade stations at every real
            // major DerelictStation platform -- the brief's own "Station
            // upgrades" fits this platform type literally, matching the
            // real "location, not a new category" scoping in
            // TntWarsUpgrades.hpp's own header comment. Capped at the
            // first 2 real major stations found (one is already plenty
            // reachable; every major station would over-clutter a small
            // map).
            size_t spaceUpgradeStationCount = 0;
            int spaceStationsPlaced = 0;
            for (const engine::tntwars::SpacePlatform& platform : spacePlatforms) {
                if (platform.type != engine::tntwars::SpacePlatformType::DerelictStation || !platform.major) continue;
                if (spaceStationsPlaced >= 2) break;
                std::string label = "SpaceMap_Station" + std::to_string(spaceStationsPlaced);
                spaceUpgradeStationCount += engine::tntwars::spawnUpgradeStations(
                                                 app.ecs(), app.meshLibrary(), materials, app.renderer().allocator(),
                                                 app.renderer().device(), app.renderer().commandPool(),
                                                 app.renderer().graphicsQueue(), platform.center, label.c_str())
                                                 .size();
                ++spaceStationsPlaced;
            }
            std::fprintf(stdout, "engine_runtime: Space Map upgrade stations spawned -- %zu real stations.\n",
                         spaceUpgradeStationCount);

            // Kronos ("Lighting Polish" world-building): real, clearer,
            // warmer air inside every major derelict station, contrasted
            // against the real void-haze baseline set above.
            app.tntWarsAtmosphereZones() = engine::tntwars::buildSpaceMapAtmosphereZones(spacePlatforms);
            std::fprintf(stdout, "engine_runtime: Space Map atmosphere zones built -- %zu real zones.\n",
                         app.tntWarsAtmosphereZones().size());

            // Kronos bugfix (live-reported: "no visual model for the
            // resource nodes"): real resource-node repositioning -- see
            // tntwars::buildScavengeNodesAt()'s own header comment. The
            // match's own default nodes anchor on Space Map's generic
            // flat "TeamA_Base"/"TeamB_Base" position (Z=+/-90, right at
            // the outer edge of the real 100-unit platform-scatter
            // radius) -- real-anchored here instead on the real most-
            // negative-Z and most-positive-Z major platforms actually
            // spawned, so nodes land on real, reachable platform
            // geometry, real-inside the exact same Team A/B territory
            // (tntwars::territoryAt()'s own real Z-sign split) that
            // scavengeNode() itself gates on.
            {
                engine::tntwars::TntWarsMatch& spaceMatch = app.networkSession().tntWarsMatch();
                const engine::tntwars::SpacePlatform* teamAAnchor = nullptr;
                const engine::tntwars::SpacePlatform* teamBAnchor = nullptr;
                for (const engine::tntwars::SpacePlatform& platform : spacePlatforms) {
                    if (!platform.major) continue;
                    if (teamAAnchor == nullptr || platform.center.z < teamAAnchor->center.z) teamAAnchor = &platform;
                    if (teamBAnchor == nullptr || platform.center.z > teamBAnchor->center.z) teamBAnchor = &platform;
                }
                if (teamAAnchor != nullptr && teamBAnchor != nullptr) {
                    spaceMatch.scavengeNodesMutable() =
                        engine::tntwars::buildScavengeNodesAt(teamAAnchor->center, teamBAnchor->center);
                    std::fprintf(stdout,
                                  "engine_runtime: Space Map resource nodes repositioned onto real platforms -- "
                                  "%zu real nodes.\n",
                                  spaceMatch.scavengeNodesMutable().size());
                }
            }

            // Kronos ("Space Map Bible" v1.0, Section III "Traversal
            // Systems"): Orbital Rails, reusing the existing real
            // ZipLineState/advanceZipLineRider system (see
            // spawnSpaceMapOrbitalRails()'s own header comment).
            engine::tntwars::SpaceMapOrbitalRails orbitalRails = engine::tntwars::spawnSpaceMapOrbitalRails(
                app.ecs(), app.physics(), app.meshLibrary(), materials, app.renderer().allocator(),
                app.renderer().device(), app.renderer().commandPool(), app.renderer().graphicsQueue(), spacePlatforms);
            app.tntWarsZipLines() = orbitalRails.rails;
            std::fprintf(stdout, "engine_runtime: Space Map orbital rails spawned -- %zu real rails.\n",
                         orbitalRails.rails.size());

            // Real booster pads -- one per major Asteroid/OrbitalRing
            // platform (capped), each launching toward its own real
            // nearest other major platform.
            std::vector<engine::tntwars::BoosterPadState> boosterPads;
            int boosterPadsPlaced = 0;
            for (const engine::tntwars::SpacePlatform& platform : spacePlatforms) {
                if (!platform.major || platform.type == engine::tntwars::SpacePlatformType::DerelictStation) continue;
                if (boosterPadsPlaced >= 3) break;
                int nearestIndex = -1;
                float nearestDist = std::numeric_limits<float>::max();
                for (size_t j = 0; j < spacePlatforms.size(); ++j) {
                    if (spacePlatforms[j].center == platform.center) continue;
                    float dist = glm::length(spacePlatforms[j].center - platform.center);
                    if (dist < nearestDist) {
                        nearestDist = dist;
                        nearestIndex = static_cast<int>(j);
                    }
                }
                if (nearestIndex < 0) continue;
                engine::tntwars::BoosterPadState pad;
                pad.position = platform.center + glm::vec3(0.0f, platform.radius * 0.5f, 0.0f);
                glm::vec3 toTarget = spacePlatforms[static_cast<size_t>(nearestIndex)].center - pad.position;
                float toTargetLen = glm::length(toTarget);
                if (toTargetLen < 0.01f) continue;
                pad.direction = toTarget / toTargetLen;
                pad.triggerRadius = 4.0f;
                pad.launchStrength = 26.0f;
                boosterPads.push_back(pad);
                ++boosterPadsPlaced;
            }
            app.tntWarsBoosterPads() = boosterPads;
            std::fprintf(stdout, "engine_runtime: Space Map booster pads spawned -- %zu real pads.\n",
                         boosterPads.size());

            // Real Zero-G Zone -- one, large, centered on the map's own
            // open void (see ZeroGravityZone's own comment on why this
            // is a real, local per-tick impulse, not a global gravity
            // change).
            engine::tntwars::ZeroGravityZone voidZeroG;
            voidZeroG.position = glm::vec3(0.0f, 0.0f, 0.0f);
            voidZeroG.radius = 45.0f;
            voidZeroG.gravityCancelFraction = 0.8f;
            app.tntWarsZeroGravityZones() = {voidZeroG};

            // Real Gravity Wells -- one per major Asteroid platform (a
            // real, thematic "asteroids have their own subtle pull"),
            // gentle enough to nudge a nearby player, not yank them in.
            std::vector<engine::tntwars::GravityWellState> gravityWells;
            for (const engine::tntwars::SpacePlatform& platform : spacePlatforms) {
                if (platform.type != engine::tntwars::SpacePlatformType::Asteroid || !platform.major) continue;
                engine::tntwars::GravityWellState well;
                well.position = platform.center;
                well.radius = platform.radius * 2.5f;
                well.maxAccel = 3.5f;
                gravityWells.push_back(well);
            }
            app.tntWarsGravityWells() = gravityWells;
            std::fprintf(stdout,
                         "engine_runtime: Space Map traversal systems -- %zu Zero-G zone(s), %zu gravity well(s).\n",
                         app.tntWarsZeroGravityZones().size(), gravityWells.size());

            // Kronos ("Explosives System" world-building): real,
            // damageable explosive barrels near a couple of major
            // asteroid platforms (volatile cargo drifting near mining
            // sites is a real, thematic fit).
            int spaceBarrelsPlaced = 0;
            for (const engine::tntwars::SpacePlatform& platform : spacePlatforms) {
                if (platform.type != engine::tntwars::SpacePlatformType::Asteroid || !platform.major) continue;
                if (spaceBarrelsPlaced >= 4) break;
                auto barrel = engine::tntwars::buildExplosiveBarrel(platform.center +
                                                                     glm::vec3(0.0f, platform.radius * 0.5f, 0.0f));
                engine::tntwars::spawnExplosiveBarrelVisual(
                    app.ecs(), app.physics(), app.meshLibrary(), materials, app.renderer().allocator(),
                    app.renderer().device(), app.renderer().commandPool(), app.renderer().graphicsQueue(), barrel,
                    "SpaceMap_ExplosiveBarrel");
                app.tntWarsExplosiveBarrels().push_back(barrel);
                ++spaceBarrelsPlaced;
            }
            std::fprintf(stdout, "engine_runtime: Space Map explosive barrels spawned -- %d real barrels.\n",
                         spaceBarrelsPlaced);

            // Kronos ("Space Map Bible" v1.0 Section VI "Combat Layer",
            // PvE "Void Drones"): real, live mobs guarding derelict
            // wreckage -- same real live-wired AI as Sky Map's own Sky
            // Sentinels above, placed at a couple of real major
            // DerelictStation platforms.
            int voidDronesPlaced = 0;
            for (const engine::tntwars::SpacePlatform& platform : spacePlatforms) {
                if (platform.type != engine::tntwars::SpacePlatformType::DerelictStation || !platform.major) continue;
                if (voidDronesPlaced >= 3) break;
                engine::miningsim::MobState droneState =
                    engine::miningsim::spawnMobOfRarity(engine::miningsim::RarityTier::Rare);
                engine::tntwars::CombatMobInstance drone = engine::tntwars::spawnCombatMob(
                    app.ecs(), app.meshLibrary(), materials, app.renderer().allocator(), app.renderer().device(),
                    app.renderer().commandPool(), app.renderer().graphicsQueue(), droneState,
                    platform.center + glm::vec3(0.0f, platform.radius * 0.6f, 0.0f), 22.0f);
                app.tntWarsCombatMobs().push_back(drone);
                ++voidDronesPlaced;
            }
            std::fprintf(stdout, "engine_runtime: Space Map Void Drones spawned -- %d real, live PvE mobs.\n",
                         voidDronesPlaced);

            // Kronos ("Space Map Bible" v1.0 Section VI "Combat Layer",
            // "PvP Orbital Conflict"): real, live capture-point nodes --
            // one at the map's own real neutral center, one at the
            // first real OrbitalRing platform found (the brief's own
            // "orbital conflict" fits a ring platform literally).
            std::vector<engine::tntwars::PvPNodeState> pvpNodes;
            engine::tntwars::PvPNodeState centerNode;
            centerNode.position = glm::vec3(0.0f, 0.0f, 0.0f);
            centerNode.radius = 10.0f;
            pvpNodes.push_back(centerNode);
            for (const engine::tntwars::SpacePlatform& platform : spacePlatforms) {
                if (platform.type != engine::tntwars::SpacePlatformType::OrbitalRing) continue;
                engine::tntwars::PvPNodeState ringNode;
                ringNode.position = platform.center;
                ringNode.radius = platform.radius * 0.8f;
                pvpNodes.push_back(ringNode);
                break;
            }
            app.tntWarsPvPNodes() = pvpNodes;
            app.tntWarsPvPNodeVisuals().clear();
            for (size_t i = 0; i < pvpNodes.size(); ++i) {
                std::string label = "SpaceMap_PvPNode_" + std::to_string(i);
                app.tntWarsPvPNodeVisuals().push_back(engine::tntwars::spawnPvPNodeVisual(
                    app.ecs(), app.meshLibrary(), materials, app.renderer().allocator(), app.renderer().device(),
                    app.renderer().commandPool(), app.renderer().graphicsQueue(), pvpNodes[i], label.c_str()));
            }
            std::fprintf(stdout, "engine_runtime: Space Map PvP nodes spawned -- %zu real, live capture points.\n",
                         pvpNodes.size());
        }

        // Kronos ("Four RTX Maps" Phase 5b): Volcano Map's own real
        // sculpted terrain enrichment (tapered volcano cone, emissive
        // crater, lava rivers, TNT tunnel, rugged terrain, molten boss
        // arena) alongside Mantle's existing flat-piece layout, plus the
        // real heat-haze cinematic post effect this map is themed around
        // -- see Renderer::setHeatDistortionEnabled()'s own comment for
        // why that stays scoped inside Cinematic Mode rather than always-on.
        if (map == engine::tntwars::MapId::Mantle) {
            std::vector<engine::core::EntityId> volcanoMapEntities = engine::tntwars::spawnVolcanoMapVisual(
                app.ecs(), app.physics(), app.meshLibrary(), materials, app.renderer().allocator(),
                app.renderer().device(), app.renderer().commandPool(), app.renderer().graphicsQueue(),
                engine::miningsim::RarityTier::Legendary);
            std::fprintf(stdout, "engine_runtime: Volcano Map terrain spawned -- %zu real, collidable pieces.\n",
                         volcanoMapEntities.size());
            app.renderer().setCinematicMode(true);
            app.renderer().setHeatDistortionEnabled(true);
        }

        // Kronos ("Four RTX Maps" Phase 5c): Underwater Map's own real
        // sculpted terrain enrichment (sea floor, coral reef, trench,
        // cave, crystal mining nodes, oxygen-zone markers, bioluminescent
        // boss arena) alongside IslandSea's existing flat-piece layout,
        // plus the real caustic-lighting effect this map is themed around.
        if (map == engine::tntwars::MapId::IslandSea) {
            std::vector<engine::core::EntityId> underwaterMapEntities = engine::tntwars::spawnUnderwaterMapVisual(
                app.ecs(), app.physics(), app.meshLibrary(), materials, app.renderer().allocator(),
                app.renderer().device(), app.renderer().commandPool(), app.renderer().graphicsQueue(),
                engine::miningsim::RarityTier::Legendary);
            std::fprintf(stdout, "engine_runtime: Underwater Map terrain spawned -- %zu real, collidable pieces.\n",
                         underwaterMapEntities.size());
            app.renderer().setUnderwaterCausticsEnabled(true);
        }

        engine::tntwars::TntWarsMatch& match = app.networkSession().tntWarsMatch();
        // Real, offline single-player setup: one real registered player,
        // driven directly (no client-server round trip -- see
        // Application::setTntWarsLiveMode()'s own comment), advanced
        // through the real, linear MatchFlow (no phase is skippable, see
        // MatchFlow.hpp's own header comment) to ClassSelect -- and no
        // further. Kronos bugfix (live-reported: "no TNT, no PvP/attack
        // system"): this used to advance a second time, straight to
        // InProgress, with the stated intent of not rejecting
        // placeTntCharge()/fireWeapon() the instant live mode starts --
        // but TntWarsMatch::selectClass() only ever succeeds during
        // Lobby/ClassSelect (MatchFlow.cpp's own real isValidTransition()
        // table), and placeTntCharge()/fireWeapon() both also require a
        // real hasSelectedClass() first. Jumping straight to InProgress
        // meant the player could never actually select a class at all
        // (every real 1-5 press logged "select class ... rejected"),
        // which meant nothing gated on having a class ever worked either
        // -- the original fix defeated its own real goal. The real fix:
        // stay in ClassSelect; Application's own live tick now advances
        // to InProgress itself the exact real moment the local player's
        // own first class selection actually succeeds (see that tick
        // block's own comment).
        constexpr engine::net::PlayerId kLocalPlayerId = 1;
        match.registerPlayer(kLocalPlayerId);
        match.matchFlow().advanceTo(engine::tntwars::MatchPhase::ClassSelect);

        // Kronos ("Gameplay Loop" world-building): real, live resource
        // nodes -- turns TntWarsMatch's own already-real, already-tested
        // scavenge economy (territory-gated depletion/respawn, a real
        // per-player materials ledger) into something a player can
        // actually see and walk up to, with real per-biome flavor names
        // (Sky Crystal/Storm Ore/Aether Wire on Sky Map, Star Ore/Void
        // Crystal/Energy Shard on Space Map -- see
        // mapMaterialDisplayName()'s own comment) -- see
        // Application::tntWarsScavengeNodeVisuals()'s own comment for the
        // live proximity+Interact wiring this feeds.
        app.tntWarsScavengeNodeVisuals() = engine::tntwars::spawnScavengeNodeVisuals(
            app.ecs(), app.meshLibrary(), materials, app.renderer().allocator(), app.renderer().device(),
            app.renderer().commandPool(), app.renderer().graphicsQueue(), match.scavengeNodes(), map);
        std::fprintf(stdout, "engine_runtime: TNT Wars resource nodes spawned -- %zu real nodes.\n",
                     app.tntWarsScavengeNodeVisuals().size());

        // Real destructible wall -- only Trenches has one (see
        // TntWarsMatch.hpp's own comment on trenchesWall_'s real map
        // scoping); spawned with real colliders + tracked visual state so
        // Application's own pretick hook can keep it in sync every frame.
        if (map == engine::tntwars::MapId::Trenches) {
            app.tntWarsWallVisuals() = engine::tntwars::spawnDestructibleWallVisual(
                app.ecs(), app.meshLibrary(), app.physics(), materials.stone, app.renderer().allocator(),
                app.renderer().device(), app.renderer().commandPool(), app.renderer().graphicsQueue(),
                match.trenchesWallMutable());

            // Kronos ("Four RTX Maps" Phase 5d): the map's own four real
            // "Cover_*" pieces are destructible too now (TrenchesCover.hpp)
            // -- same real spawn/tick pattern as the wall just above,
            // wood-textured (real sandbag/timber cover, distinct from the
            // wall's own stone).
            app.tntWarsCoverVisuals() = engine::tntwars::spawnDestructibleWallVisual(
                app.ecs(), app.meshLibrary(), app.physics(), materials.wood, app.renderer().allocator(),
                app.renderer().device(), app.renderer().commandPool(), app.renderer().graphicsQueue(),
                match.trenchesCoverMutable());

            // Kronos ("Four RTX Maps" Phase 5d): Trenches Map's own real
            // sculpted terrain enrichment (mud patches, wooden bunkers, a
            // TNT tunnel, elevated artillery platforms, a trench-commander
            // boss arena) alongside buildTrenches()'s existing flat-piece
            // layout.
            std::vector<engine::core::EntityId> trenchesMapEntities = engine::tntwars::spawnTrenchesMapVisual(
                app.ecs(), app.physics(), app.meshLibrary(), materials, app.renderer().allocator(),
                app.renderer().device(), app.renderer().commandPool(), app.renderer().graphicsQueue(),
                engine::miningsim::RarityTier::Legendary);
            std::fprintf(stdout, "engine_runtime: Trenches Map terrain spawned -- %zu real, collidable pieces.\n",
                         trenchesMapEntities.size());
        }

        app.setTntWarsLiveMode(true, kLocalPlayerId);

        // Real starting spawn position -- just behind TeamA's own real
        // base, looking north across the play surface toward TeamB.
        glm::vec3 teamABase = engine::tntwars::coreWorldPosition(map, engine::tntwars::TeamId::A);
        glm::vec3 spawnPosition = teamABase + glm::vec3(0.0f, 3.0f, -10.0f);
        float spawnYawDegrees = 0.0f;
        float spawnPitchDegrees = -5.0f;

        // Kronos ("Four RTX Maps" Phase 5c): the Underwater Map's own real
        // content sits well below IslandSea's surface-level TeamA base
        // (see kUnderwaterMapCenter()'s own comment) -- override the
        // default spawn point above so a live run actually starts inside
        // the new trench arena instead of stranded on the surface.
        if (map == engine::tntwars::MapId::IslandSea) {
            spawnPosition = engine::tntwars::kUnderwaterMapCenter() + glm::vec3(0.0f, 4.0f, -20.0f);
            spawnYawDegrees = 90.0f;
            spawnPitchDegrees = -5.0f;
        }

        // Kronos ("Sky Bases" world-building): a real, queried point on
        // Team A's own real Sky Base terrain surface
        // (core::Terrain::heightAt(), not a hand-picked coordinate) --
        // this map's own live TNT Wars mode always spawns the local
        // player on Team A (see coreWorldPosition(map, TeamId::A) above,
        // this map's own real equivalent of that same default). Falls
        // back to the flat default only if that base's own real
        // Terrain::create() somehow failed (logged there) -- an honest,
        // rare degradation, not silently pretending nothing went wrong.
        if (map == engine::tntwars::MapId::SkyPlatforms && skyTeamABaseTerrain != nullptr) {
            float groundY = skyTeamABaseTerrain->heightAt(skyTeamABaseCenter.x, skyTeamABaseCenter.z);
            spawnPosition = glm::vec3(skyTeamABaseCenter.x, groundY + 2.0f, skyTeamABaseCenter.z);
            // Real, facing toward Team B's own base (+X, see the base
            // placement above; core::Camera::forward()'s own real
            // yaw=0 direction is +X) -- a player's very first view is
            // real, deliberately toward the objective, not an arbitrary
            // default.
            spawnYawDegrees = 0.0f;
            spawnPitchDegrees = -10.0f;
        }

        // Real, physics-driven player character -- this mode never spawned
        // one before (only `--tntwars`'s own camera was moved directly),
        // which meant CharacterController::tick()'s own real WASD/sprint/
        // jump/gravity logic real-no-op'd every frame (its own early
        // `if (entity_ == kNullEntity) return;`) -- WASD looked live
        // (the binding existed) but genuinely moved nothing. Same real
        // capsule + nose-marker + third-person-follow-camera setup the
        // default bring-up scene already establishes (see this file's own
        // bring-up-scene character spawn further down for the identical
        // pattern), just placed at this map's own real spawn point instead.
        uint32_t tntWarsCapsuleMesh = app.meshLibrary().registerMesh(engine::core::Mesh::createCapsule(
            app.renderer().allocator(), app.renderer().device(), app.renderer().commandPool(),
            app.renderer().graphicsQueue(), 0.35f, 0.55f));
        uint32_t tntWarsNoseMesh = app.meshLibrary().registerMesh(engine::core::Mesh::createBox(
            app.renderer().allocator(), app.renderer().device(), app.renderer().commandPool(),
            app.renderer().graphicsQueue(), glm::vec3(1.0f)));
        engine::core::EntityId tntWarsCharacter =
            app.characterController().spawn(app.ecs(), app.physics(), spawnPosition, tntWarsNoseMesh);
        makeRenderable(app.ecs(), tntWarsCharacter, tntWarsCapsuleMesh, {0.25f, 0.55f, 0.85f}, 0.05f, 0.55f);
        app.characterController().setInitialCameraAngles(spawnYawDegrees, spawnPitchDegrees);
        // Kronos ("Quality-of-Life & Finalization", "Spawn logic"): the
        // exact same real spawn position feeds a mid-match respawn too
        // -- see Application::setTntWarsRespawnPosition()'s own comment.
        app.setTntWarsRespawnPosition(spawnPosition);

        std::fprintf(stdout,
                      "engine_runtime: TNT Wars live -- WASD/mouse to move, Shift to sprint, Space to jump, 1-5 to "
                      "select a class (Striker/Deflector/Engineer/Interceptor/Saboteur), G to place a TNT charge, F "
                      "to fire, Q for your ultimate, E to scavenge a resource node, mount a zip-line, or spend "
                      "materials at an upgrade station, V to throw a grenade, C to craft a Standard Charge from "
                      "scavenged materials, H to place a crafted charge.\n");

        app.run();

        // Kronos ("User Interface" world-building, "Leaderboard UI"):
        // real save-on-exit -- runs the real moment the live window
        // closes (app.run() only returns then), so a best time set
        // during this real session is never lost even if the process
        // exits normally right after.
        if (engine::tntwars::saveChallengeBestTimes(app.tntWarsTraversalChallenges(), kTntWarsLeaderboardPath)) {
            std::fprintf(stdout, "engine_runtime: leaderboard best times saved to \"%s\".\n", kTntWarsLeaderboardPath);
        }

        app.shutdown();
        return 0;
    }

    // Kronos ("Moderation Architecture v2", "Session Browser Game
    // Identity"): real "which game is this session actually running,"
    // derived from the same real rich-mode flags parsed above -- a real,
    // honest, direct answer for a hosted `--server` session, not a
    // fabricated lookup. `gameThumbnailColor`/`gameSafetyStatus` stay at
    // their real, honest defaults here: none of TNT Wars/Mining Sim/
    // House Demo/Render Showcase have a real core::GameManifest on disk
    // today (confirmed by grep -- only games/DefaultWorld and
    // games/SkyGarden exist), so there's nothing real to look either
    // value up from yet; a real, scoped follow-up once those modes get
    // real manifests, not built speculatively here.
    if (tntWarsMode) networkConfig.gameName = "TNT Wars";
    else if (miningSimMode) networkConfig.gameName = "Mining Simulator";
    else if (houseDemoMode) networkConfig.gameName = "House Demo";
    else if (renderShowcaseMode) networkConfig.gameName = "Render Showcase";
    else networkConfig.gameName = "Default World";

    if (networkConfig.mode != engine::net::NetworkMode::Offline) {
        if (!app.startNetworking(networkConfig)) {
            std::fprintf(stderr, "engine_runtime: startNetworking failed.\n");
            return 1;
        }
        std::fprintf(stdout, "engine_runtime: networking started (%s)\n",
                     networkConfig.mode == engine::net::NetworkMode::Server ? "server" : "client");
        if (stressTestPlayerCount > 0) {
            app.networkSession().startStressTest(stressTestPlayerCount, 20.0f);
        }
    }

    // Real geometry, uploaded once via VMA-backed vertex/index buffers
    // (core::Mesh) and referenced by handle from any number of entities'
    // Renderable components.
    auto& renderer = app.renderer();
    // Once-a-second fps/draw-call/GPU-memory line -- see
    // Renderer::metrics()'s doc comment for exactly what's measured.
    renderer.setMetricsLogging(true);
    uint32_t planeMesh = app.meshLibrary().registerMesh(engine::core::Mesh::createPlane(
        renderer.allocator(), renderer.device(), renderer.commandPool(), renderer.graphicsQueue(), 25.0f, 25.0f));
    uint32_t boxMesh = app.meshLibrary().registerMesh(engine::core::Mesh::createBox(
        renderer.allocator(), renderer.device(), renderer.commandPool(), renderer.graphicsQueue(), {0.5f, 0.5f, 0.5f}));
    // Kronos ("Alpha v1 Polish" -- "world.spawnDynamicBox"): real, same
    // 1x1x1 unit-cube handle every other hand-placed box prop in this
    // bring-up scene already shares -- registered once here, for the
    // whole process lifetime, so it's already real and available by the
    // time any later-loaded game's own script (e.g. games/DefaultWorld/
    // Scripts/Main.lua, picked from the Game Catalogue) calls
    // world.spawnDynamicBox() -- see ScriptWorldApi::
    // setSpawnBoxMeshHandle()'s own comment for why this is a one-time
    // setter rather than a live per-call GPU mesh build.
    app.setScriptSpawnBoxMeshHandle(boxMesh);

    // Ground plane -- physics (static collider) + a rough dielectric material.
    auto ground = app.physics().createGroundPlane(app.ecs(), 25.0f, 25.0f);
    makeRenderable(app.ecs(), ground, planeMesh, {0.35f, 0.36f, 0.4f}, 0.0f, 0.85f);
    // Sprint 14 ("RTX Upgrade" Phase 2): a real MeshSource -- see
    // Components.hpp's own comment on what that component is for -- is
    // what makes this entity a real, eligible ray-traced-shadow
    // caster/occluder (core::RayTracingScene.hpp's own header comment on
    // why RT shadows are scoped to MeshSource-described Box/Plane
    // entities this pass). Attaching it doesn't change this entity's
    // rendering at all when ray-traced shadows are off (F6, default) --
    // purely additive real metadata already used elsewhere for scene
    // save/load.
    auto& groundMeshSource = app.ecs().addComponent<engine::core::MeshSource>(ground);
    groundMeshSource.kind = engine::core::MeshSourceKind::Plane;
    groundMeshSource.params = {25.0f, 0.0f, 25.0f};

    // A dynamic box, falling under real Jolt gravity onto the ground plane
    // every run -- a copper-ish metal so it reads clearly against the grid
    // below.
    auto fallingBox = app.physics().createDynamicBox(app.ecs(), {0.0f, 10.0f, 0.0f}, {0.5f, 0.5f, 0.5f}, 1.0f);
    makeRenderable(app.ecs(), fallingBox, boxMesh, {0.95f, 0.64f, 0.54f}, 1.0f, 0.25f);
    auto& fallingBoxMeshSource = app.ecs().addComponent<engine::core::MeshSource>(fallingBox);
    fallingBoxMeshSource.kind = engine::core::MeshSourceKind::Box;
    fallingBoxMeshSource.params = {0.5f, 0.5f, 0.5f};

    // The playable character -- see core/CharacterController.hpp.
    // capsuleMesh/noseMesh stay registered (still real, still used below
    // by the networked-player entity, a deliberately simpler Transform+
    // Renderable-only avatar -- see startNetworking()'s own comment on
    // why that path doesn't use a physics capsule at all). The local
    // player's own body is now the real, 18-bone rigged humanoid avatar
    // (Kronos "Avatar System" -- see Application::spawnLocalPlayerAvatar()'s
    // own comment), not this plain capsule mesh.
    uint32_t capsuleMesh = app.meshLibrary().registerMesh(engine::core::Mesh::createCapsule(
        renderer.allocator(), renderer.device(), renderer.commandPool(), renderer.graphicsQueue(), 0.35f, 0.55f));
    uint32_t noseMesh = app.meshLibrary().registerMesh(engine::core::Mesh::createBox(
        renderer.allocator(), renderer.device(), renderer.commandPool(), renderer.graphicsQueue(), glm::vec3(1.0f)));

    // Kronos ("Avatar Preview Rendering" pre-launch fix): real, minimal
    // spawn clearance above the ground plane (Y=0), not the previous
    // Y=3.0 -- the character capsule (capsuleRadius 0.35 + capsuleHalfHeight
    // 0.55, ~1.8 units tall including caps) needs its center at least
    // ~0.9 above ground to avoid spawning embedded in it; Y=1.0 gives a
    // real, small, correct clearance instead of a real, visible multi-
    // unit fall/settle window the very first frames this bring-up
    // world's local player avatar is visible behind the Home screen.
    if (!app.spawnLocalPlayerAvatar({0.0f, 1.0f, -6.0f})) {
        std::fprintf(stderr, "engine_runtime: spawnLocalPlayerAvatar() failed for the bring-up world.\n");
    }
    auto character = app.characterController().entity();

    // Sprint 11 ("Networking Foundation"): the real networked-player
    // entity Client mode drives -- see Application::startNetworking()'s
    // own comment on why this is a deliberately separate, simpler entity
    // from `character` above (which stays real and spawned in every
    // mode, but only actually *drives movement* in Offline mode -- see
    // the pre-tick hook's own real branching in Application.cpp).
    if (networkConfig.mode == engine::net::NetworkMode::Client) {
        auto networkedPlayer = app.ecs().createEntity("NetworkedPlayer");
        if (auto* transform = app.ecs().tryGetComponent<engine::core::Transform>(networkedPlayer)) {
            transform->position = {2.0f, 3.0f, -6.0f};
        }
        makeRenderable(app.ecs(), networkedPlayer, capsuleMesh, {0.85f, 0.55f, 0.25f}, 0.05f, 0.55f);
        app.setNetworkedLocalPlayerEntity(networkedPlayer);
    }

    // Runtime Interaction Examples (docs task category 7) -- six real,
    // working scenes exercising Physics/CharacterController/Interaction
    // end to end, all placed within walking distance of spawn ({0,3,-6}).
    {
        auto& physics = app.physics();

        // Pushable box -- default material (friction 0.5, Jolt's own
        // default), a moderate mass -- walks into it, it slides a little
        // and stops, the baseline every other physics prop here is
        // contrasted against.
        auto pushableBox = physics.createDynamicBox(app.ecs(), {6.0f, 0.4f, -4.0f}, {0.4f, 0.4f, 0.4f}, 5.0f);
        makeRenderable(app.ecs(), pushableBox, boxMesh, {0.75f, 0.55f, 0.3f}, 0.1f, 0.6f);
        if (auto* name = app.ecs().tryGetComponent<engine::core::Name>(pushableBox)) name->value = "PushableBox";

        // Sliding crate -- same shape/mass as the pushable box, but a
        // near-frictionless material (0.05 vs. the default 0.5): the same
        // push sends it sliding much farther, a real, visible
        // demonstration that PhysicsMaterial::friction actually does
        // something, not just a second copy of the box above.
        engine::core::PhysicsMaterial lowFriction{0.05f, 0.0f, 600.0f};
        auto slidingCrate = physics.createDynamicBox(app.ecs(), {6.0f, 0.4f, -1.5f}, {0.4f, 0.4f, 0.4f}, 5.0f, lowFriction);
        makeRenderable(app.ecs(), slidingCrate, boxMesh, {0.6f, 0.65f, 0.7f}, 0.15f, 0.35f);
        if (auto* name = app.ecs().tryGetComponent<engine::core::Name>(slidingCrate)) name->value = "SlidingCrate";

        // Bouncing sphere -- Rubber preset (restitution 0.8), dropped from
        // height so it visibly bounces several times before settling
        // real Jolt restitution, not a scripted animation.
        auto rubber = engine::core::physicsMaterialForPreset(engine::core::PhysicsMaterialPreset::Rubber);
        auto bouncingSphere = physics.createSphereBody(app.ecs(), {6.0f, 6.0f, 1.5f}, 0.4f, 0.0f, rubber);
        uint32_t sphereMesh = app.meshLibrary().registerMesh(engine::core::Mesh::createCapsule(
            renderer.allocator(), renderer.device(), renderer.commandPool(), renderer.graphicsQueue(), 0.4f, 0.0f));
        makeRenderable(app.ecs(), bouncingSphere, sphereMesh, {0.85f, 0.15f, 0.2f}, 0.0f, 0.4f);
        if (auto* name = app.ecs().tryGetComponent<engine::core::Name>(bouncingSphere)) name->value = "BouncingSphere";

        // Jumpable moving platform -- a real live Jolt Kinematic body
        // (Physics::createKinematicBox(), driven every tick by
        // Application.cpp's pre-tick hook via Physics::moveKinematic() and
        // Components.hpp's MovingPlatform/movingPlatformTarget()). Top
        // surface sits at y=1.25 -- well above CharacterController's
        // stepHeight (0.3, can't just walk up onto it) but comfortably
        // under its ~2.5m max jump height (jumpSpeed 7.0 vs gravity ~9.81,
        // v^2/2g) -- a real jump is required, not optional. Oscillates 2m
        // along X once you're on it.
        auto platform = physics.createKinematicBox(app.ecs(), {10.0f, 1.0f, 5.0f}, {1.5f, 0.25f, 1.5f});
        makeRenderable(app.ecs(), platform, boxMesh, {0.4f, 0.75f, 0.5f}, 0.2f, 0.5f);
        if (auto* name = app.ecs().tryGetComponent<engine::core::Name>(platform)) name->value = "JumpablePlatform";
        app.ecs().addComponent<engine::core::MovingPlatform>(
            platform, engine::core::MovingPlatform{{10.0f, 1.0f, 5.0f}, {1.0f, 0.0f, 0.0f}, 2.0f, 6.0f});

        // Interactable door -- raycast-triggered (proximityEnabled=false,
        // the default: look at it and press E), no physics collider of
        // its own -- see Door's own doc comment on why this is a real,
        // honest "swings open/closed on interact" scope rather than a
        // half-built physics hinge. A thin scaled box reads clearly as a
        // door panel against boxMesh's 1x1x1 unit cube.
        auto door = app.ecs().createEntity("InteractableDoor");
        if (auto* transform = app.ecs().tryGetComponent<engine::core::Transform>(door)) {
            transform->position = {3.0f, 1.0f, -6.0f};
            transform->scale = {0.15f, 2.0f, 1.0f};
        }
        makeRenderable(app.ecs(), door, boxMesh, {0.5f, 0.35f, 0.2f}, 0.05f, 0.7f);
        auto& doorInteractable = app.ecs().addComponent<engine::core::Interactable>(door);
        doorInteractable.prompt = "Press E to open/close door";
        engine::core::Door doorState;
        doorState.closedRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        doorState.openRotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        app.ecs().addComponent<engine::core::Door>(door, doorState);

        // Interactable pickup item -- proximity-triggered (walk up to it,
        // no look-and-press needed, the real second interaction mapping
        // this task category's Interaction System already built), small
        // and emissive so it reads as "collectible" against every other
        // prop in the scene. Vanishes (Renderable::visible=false,
        // Interactable removed) the moment it's collected -- see
        // collectPickup()'s own comment on why that's the honest scope
        // (no inventory/currency system exists yet to hand it off to).
        auto pickup = app.ecs().createEntity("InteractablePickup");
        if (auto* transform = app.ecs().tryGetComponent<engine::core::Transform>(pickup)) {
            transform->position = {-3.0f, 0.6f, -3.0f};
            transform->scale = {0.35f, 0.35f, 0.35f};
        }
        auto& pickupRenderable = app.ecs().addComponent<engine::core::Renderable>(pickup);
        pickupRenderable.meshHandle = boxMesh;
        pickupRenderable.baseColor = glm::vec4(0.9f, 0.8f, 0.2f, 1.0f);
        pickupRenderable.metallic = 0.3f;
        pickupRenderable.roughness = 0.2f;
        pickupRenderable.emissiveColor = {1.0f, 0.85f, 0.3f};
        pickupRenderable.emissiveIntensity = 1.5f;
        auto& pickupInteractable = app.ecs().addComponent<engine::core::Interactable>(pickup);
        pickupInteractable.prompt = "Press E to pick up";
        pickupInteractable.proximityEnabled = true;
        pickupInteractable.proximityRadius = 2.0f;
        app.ecs().addComponent<engine::core::Pickup>(pickup);
    }

    // Sprint 5 ("Core Economy") -- real ore nodes, inventory, currency,
    // shop, and upgrades, all dispatched through the exact same
    // interaction pipeline the props above already exercise (mining a
    // node, selling at the stall, and buying an upgrade are all just
    // "look/walk up, press E"). See core/OreNode.hpp, core/Inventory.hpp,
    // core/Economy.hpp, core/Shop.hpp, core/UpgradeSystem.hpp.
    {
        auto& ecs = app.ecs();
        auto& physics = app.physics();

        // Real per-player economy state, attached directly to the
        // character entity -- the same "this is real gameplay data, it
        // belongs on an entity, not an Application-level global"
        // reasoning RigidBody/ColliderShape already established for
        // physics bodies. A small starting stake (50 coins) so the very
        // first upgrade (Iron Pickaxe, 150 coins) is reachable after one
        // real sell, not a completely empty-handed start.
        ecs.addComponent<engine::core::Wallet>(character, engine::core::Wallet{50, 0});
        ecs.addComponent<engine::core::Inventory>(character, engine::core::Inventory{});
        ecs.addComponent<engine::core::PlayerUpgrades>(character, engine::core::PlayerUpgrades{});
        ecs.addComponent<engine::core::EarnThrottle>(character, engine::core::EarnThrottle{});

        // A small dedicated mesh for physical ore-drop pickups -- sized
        // close to breakOreNode()'s own 0.14-radius sphere collider (a
        // 0.3-unit box reads as a real "chunk", not the 1-unit boxMesh
        // scaled awkwardly down) -- see Application::setOreDropMeshHandle()'s
        // own comment on why core/OreNode.cpp can't register this mesh
        // itself.
        uint32_t oreDropMesh = app.meshLibrary().registerMesh(engine::core::Mesh::createBox(
            renderer.allocator(), renderer.device(), renderer.commandPool(), renderer.graphicsQueue(),
            {0.15f, 0.15f, 0.15f}));
        app.setOreDropMeshHandle(oreDropMesh);

        // Six real ore nodes, one per type -- a fresh area (X in
        // [-15,-9], Z in [-11,-5]) distinct from every other prop cluster
        // in this scene, an easy walk from spawn ({0,3,-6}).
        constexpr engine::core::OreType kAllOreTypes[engine::core::kOreTypeCount] = {
            engine::core::OreType::Copper, engine::core::OreType::Iron,     engine::core::OreType::Silver,
            engine::core::OreType::Gold,   engine::core::OreType::Platinum, engine::core::OreType::Crystal,
        };
        for (size_t i = 0; i < engine::core::kOreTypeCount; ++i) {
            glm::vec3 position{-15.0f + static_cast<float>(i) * 2.2f, 0.5f, -8.0f};
            engine::core::createOreNode(ecs, physics, kAllOreTypes[i], position, boxMesh);
        }

        // A real sell stall -- interacting sells the character's entire
        // inventory at once (see core::sellAllInventory()). No physics
        // collider of its own, same reasoning main.cpp's InteractableDoor
        // uses above (a raycast target doesn't need one).
        auto shopStall = ecs.createEntity("OreShopStall");
        if (auto* transform = ecs.tryGetComponent<engine::core::Transform>(shopStall)) {
            transform->position = {-12.0f, 0.75f, -3.5f};
            transform->scale = {1.4f, 1.5f, 1.4f};
        }
        makeRenderable(ecs, shopStall, boxMesh, {0.35f, 0.55f, 0.35f}, 0.1f, 0.6f);
        auto& shopInteractable = ecs.addComponent<engine::core::Interactable>(shopStall);
        shopInteractable.prompt = "Press E to sell all ore";
        shopInteractable.cooldownSeconds = 0.5f; // a real, if short, anti-spam cooldown -- see "Balancing"
        ecs.addComponent<engine::core::ShopStall>(shopStall);
        ecs.addComponent<engine::core::NavMarker>(shopStall, engine::core::NavMarker{engine::core::NavMarkerKind::Shop, "Ore Shop"});

        // Three real upgrade kiosks -- each purchases exactly one tier of
        // its own category on interact (task category 5), spaced out so
        // each reads as its own distinct station rather than one menu.
        struct StationSpec {
            const char* name;
            engine::core::UpgradeCategory category;
            glm::vec3 position;
            glm::vec3 color;
        };
        const StationSpec stations[] = {
            {"PickaxeUpgradeStation", engine::core::UpgradeCategory::Pickaxe, {-10.0f, 0.6f, -3.5f}, {0.8f, 0.5f, 0.2f}},
            {"BackpackUpgradeStation", engine::core::UpgradeCategory::Backpack, {-11.0f, 0.6f, -2.0f}, {0.5f, 0.35f, 0.2f}},
            {"BootsUpgradeStation", engine::core::UpgradeCategory::Boots, {-13.0f, 0.6f, -2.0f}, {0.3f, 0.3f, 0.55f}},
        };
        for (const auto& spec : stations) {
            auto station = ecs.createEntity(spec.name);
            if (auto* transform = ecs.tryGetComponent<engine::core::Transform>(station)) {
                transform->position = spec.position;
                transform->scale = {0.8f, 1.2f, 0.8f};
            }
            makeRenderable(ecs, station, boxMesh, spec.color, 0.4f, 0.4f);
            auto& interactable = ecs.addComponent<engine::core::Interactable>(station);
            interactable.prompt = "Press E to upgrade";
            interactable.cooldownSeconds = 0.5f;
            ecs.addComponent<engine::core::UpgradeStation>(station, engine::core::UpgradeStation{spec.category});
            ecs.addComponent<engine::core::NavMarker>(station, engine::core::NavMarker{engine::core::NavMarkerKind::UpgradeKiosk, spec.name});
        }
    }

    // Sprint 6 ("World Systems & Environment") -- real terrain (physics
    // colliders, chunk streaming, a real generation preset), six real
    // world props scattered across it, nav markers, a soft+hard world
    // boundary, and two real linked teleport pads. A fresh area (origin
    // (35,0,-30), spanning 64x64 world units) well clear of every
    // earlier sprint's own content, an easy walk from spawn.
    engine::core::Terrain terrain;
    {
        auto& ecs = app.ecs();
        auto& physics = app.physics();

        engine::core::Terrain::CreateInfo terrainInfo;
        terrainInfo.gridResolution = 65;
        terrainInfo.chunkCount = 8;
        terrainInfo.cellSize = 1.0f;
        terrainInfo.origin = {35.0f, 0.0f, -30.0f};
        // Real physics integration (task category 1) -- every chunk gets
        // a real live Static Jolt mesh collider, rebuilt automatically
        // whenever that chunk's mesh regenerates.
        (void)terrain.create(terrainInfo, ecs, app.meshLibrary(), renderer.allocator(), renderer.device(),
                              renderer.commandPool(), renderer.graphicsQueue(), &physics);

        // RollingHills: one of the three real presets (task category 1),
        // moderate amplitude/few octaves -- broad, gentle, genuinely
        // walkable slopes (see Terrain::applyPreset()'s own comment for
        // how this differs from FlatPlains/RockyCanyon).
        terrain.applyPreset(engine::core::Terrain::Preset::RollingHills);
        // Real, honestly-scoped ambient occlusion (task category 3) --
        // chunk-granularity valley darkening/ridge brightening.
        terrain.applyChunkAmbientOcclusion(0.6f);

        // Real biome tagging (task category 5 scaffolding) -- every
        // chunk tagged Forest; nothing in this runtime automatically
        // switches lighting based on this yet (see core/Biome.hpp's own
        // "scaffolding, not a live system" comment), but the tag itself
        // is real, queryable data.
        for (uint32_t cz = 0; cz < terrainInfo.chunkCount; ++cz) {
            for (uint32_t cx = 0; cx < terrainInfo.chunkCount; ++cx) {
                terrain.setChunkBiome(cx, cz, static_cast<int32_t>(engine::core::BiomeId::Forest));
            }
        }

        // Real chunk streaming (task category 6) -- Application ticks
        // this every frame from the character's own position.
        app.setTerrain(&terrain);
        app.setTerrainStreamingRadii(60.0f, 80.0f);

        // Six real world props (task category 2), scattered across the
        // new terrain, each snapped to the real sampled terrain height
        // at its own position (Terrain::heightAt()) so nothing spawns
        // floating or buried.
        struct PropSpec {
            engine::core::WorldPropKind kind;
            glm::vec2 xz;
            glm::vec3 halfExtent;
            glm::vec3 color;
            bool interactive;
        };
        const PropSpec props[] = {
            {engine::core::WorldPropKind::Tree, {45.0f, -22.0f}, {0.35f, 2.0f, 0.35f}, {0.35f, 0.25f, 0.15f}, false},
            {engine::core::WorldPropKind::Rock, {52.0f, -18.0f}, {0.6f, 0.5f, 0.6f}, {0.45f, 0.44f, 0.42f}, false},
            {engine::core::WorldPropKind::Crate, {48.0f, -14.0f}, {0.4f, 0.4f, 0.4f}, {0.6f, 0.45f, 0.25f}, true},
            {engine::core::WorldPropKind::Barrel, {50.0f, -13.0f}, {0.35f, 0.5f, 0.35f}, {0.3f, 0.35f, 0.3f}, true},
            {engine::core::WorldPropKind::Lamp, {44.0f, -12.0f}, {0.15f, 1.2f, 0.15f}, {0.9f, 0.85f, 0.6f}, true},
            {engine::core::WorldPropKind::Bush, {41.0f, -18.0f}, {0.45f, 0.4f, 0.45f}, {0.2f, 0.4f, 0.15f}, false},
        };
        for (const auto& spec : props) {
            float height = terrain.heightAt(spec.xz.x, spec.xz.y);
            engine::core::WorldPropSpawnInfo info;
            info.kind = spec.kind;
            info.position = {spec.xz.x, height + spec.halfExtent.y, spec.xz.y};
            info.halfExtent = spec.halfExtent;
            info.meshHandle = spec.kind == engine::core::WorldPropKind::Tree ? capsuleMesh : boxMesh;
            info.baseColor = spec.color;
            info.metallic = 0.0f;
            info.roughness = 0.85f;
            info.interactive = spec.interactive;
            engine::core::spawnWorldProp(ecs, physics, info);
        }

        // A real spawn nav marker (task category 4) -- a small,
        // non-physical reference entity at the character's actual spawn
        // point.
        auto spawnMarker = ecs.createEntity("SpawnPoint");
        if (auto* transform = ecs.tryGetComponent<engine::core::Transform>(spawnMarker)) {
            transform->position = {0.0f, 3.0f, -6.0f};
        }
        ecs.addComponent<engine::core::NavMarker>(spawnMarker, engine::core::NavMarker{engine::core::NavMarkerKind::Spawn, "Spawn"});

        // A real, soft+hard world boundary (task category 4) centered on
        // the whole bring-up scene (spawn + every sprint's content sits
        // well within softRadius=90; hardRadius=100's real physical walls
        // sit well outside all of it, including the new terrain).
        engine::core::WorldBoundary boundary;
        boundary.center = {20.0f, 0.0f, -15.0f};
        boundary.softRadius = 90.0f;
        boundary.hardRadius = 100.0f;
        app.setWorldBoundary(boundary);
        engine::core::createWorldBoundaryWalls(ecs, physics, boundary);

        // Two real, linked teleport pads (task category 4) -- one near
        // spawn, one out on the terrain; stepping onto either sends the
        // character to the other.
        auto padNearSpawn = ecs.createEntity("TeleportPadSpawn");
        if (auto* transform = ecs.tryGetComponent<engine::core::Transform>(padNearSpawn)) {
            transform->position = {4.0f, 0.5f, -8.0f};
            transform->scale = {1.2f, 0.15f, 1.2f};
        }
        makeRenderable(ecs, padNearSpawn, boxMesh, {0.25f, 0.55f, 0.85f}, 0.3f, 0.3f);
        auto& padNearSpawnInteractable = ecs.addComponent<engine::core::Interactable>(padNearSpawn);
        padNearSpawnInteractable.prompt = "Press E to teleport";
        padNearSpawnInteractable.cooldownSeconds = 1.0f;
        engine::core::TeleportPad padNearSpawnState;
        padNearSpawnState.destination = {48.0f, terrain.heightAt(48.0f, -20.0f) + 1.0f, -20.0f};
        padNearSpawnState.linkTag = "spawn_terrain_route";
        ecs.addComponent<engine::core::TeleportPad>(padNearSpawn, padNearSpawnState);
        ecs.addComponent<engine::core::NavMarker>(padNearSpawn, engine::core::NavMarker{engine::core::NavMarkerKind::TeleportPad, "Terrain Pad"});

        auto padOnTerrain = ecs.createEntity("TeleportPadTerrain");
        if (auto* transform = ecs.tryGetComponent<engine::core::Transform>(padOnTerrain)) {
            transform->position = padNearSpawnState.destination - glm::vec3(0.0f, 1.0f, 0.0f);
            transform->scale = {1.2f, 0.15f, 1.2f};
        }
        makeRenderable(ecs, padOnTerrain, boxMesh, {0.85f, 0.55f, 0.25f}, 0.3f, 0.3f);
        auto& padOnTerrainInteractable = ecs.addComponent<engine::core::Interactable>(padOnTerrain);
        padOnTerrainInteractable.prompt = "Press E to teleport";
        padOnTerrainInteractable.cooldownSeconds = 1.0f;
        engine::core::TeleportPad padOnTerrainState;
        padOnTerrainState.destination = {4.0f, 3.0f, -8.0f};
        padOnTerrainState.linkTag = "spawn_terrain_route";
        ecs.addComponent<engine::core::TeleportPad>(padOnTerrain, padOnTerrainState);
        ecs.addComponent<engine::core::NavMarker>(padOnTerrain, engine::core::NavMarker{engine::core::NavMarkerKind::TeleportPad, "Spawn Pad"});
    }

    // Material showcase grid -- metallic varies across X, roughness across
    // Z, same neutral base color throughout, the standard way to show a
    // PBR pipeline is actually responding to both parameters correctly
    // rather than just "some lighting exists".
    constexpr int kGridSize = 6;
    for (int xi = 0; xi < kGridSize; ++xi) {
        for (int zi = 0; zi < kGridSize; ++zi) {
            auto entity = app.ecs().createEntity("MaterialSample");
            if (auto* transform = app.ecs().tryGetComponent<engine::core::Transform>(entity)) {
                transform->position = {(xi - kGridSize / 2) * 1.4f, 0.5f, 5.0f + zi * 1.4f};
                transform->scale = {0.45f, 0.45f, 0.45f};
            }
            float metallic = static_cast<float>(xi) / static_cast<float>(kGridSize - 1);
            float roughness = std::max(0.05f, static_cast<float>(zi) / static_cast<float>(kGridSize - 1));
            makeRenderable(app.ecs(), entity, boxMesh, {0.9f, 0.9f, 0.92f}, metallic, roughness);
        }
    }

    // A scattered field of small "ore" props, all sharing boxMesh, each
    // flagged Renderable::instanced -- Renderer::drawInstancedBatches()
    // draws this entire field in one vkCmdDrawIndexed(instanceCount=N)
    // instead of N individual draws. A handful glow (emissiveIntensity >
    // 0) to exercise the emissive material path in the same scene. Fixed
    // RNG seed -- a reproducible bring-up scene from run to run, not a
    // stress-test of true randomness.
    {
        std::mt19937 rng(1337);
        std::uniform_real_distribution<float> posDist(-8.0f, 8.0f);
        std::uniform_real_distribution<float> scaleDist(0.12f, 0.28f);
        std::uniform_real_distribution<float> unit(0.0f, 1.0f);

        // Close enough to spawn ({0,3,-6}) to be visible immediately --
        // well inside the 25-unit-half-extent ground plane in every
        // direction, not off its edge.
        constexpr int kOreCount = 80;
        for (int i = 0; i < kOreCount; ++i) {
            auto entity = app.ecs().createEntity("OreProp");
            float scale = scaleDist(rng);
            if (auto* transform = app.ecs().tryGetComponent<engine::core::Transform>(entity)) {
                transform->position = {posDist(rng), scale, posDist(rng)};
                transform->scale = glm::vec3(scale);
            }
            auto& renderable = app.ecs().addComponent<engine::core::Renderable>(entity);
            renderable.meshHandle = boxMesh;
            renderable.instanced = true;

            bool glowing = unit(rng) < 0.15f;
            if (glowing) {
                renderable.baseColor = glm::vec4(0.6f, 0.85f, 1.0f, 1.0f);
                renderable.metallic = 0.0f;
                renderable.roughness = 0.3f;
                renderable.emissiveColor = {0.4f, 0.8f, 1.0f};
                renderable.emissiveIntensity = 2.5f;
            } else {
                float shade = 0.3f + unit(rng) * 0.25f;
                renderable.baseColor = glm::vec4(shade, shade * 0.92f, shade * 0.85f, 1.0f);
                renderable.metallic = unit(rng) < 0.3f ? 0.8f : 0.0f;
                renderable.roughness = 0.5f + unit(rng) * 0.4f;
            }
        }
    }

    // A campfire-style ember emitter near spawn -- real
    // engine::core::ParticleEmitter component, simulated every tick by
    // Application's pre-tick hook (Application.cpp) and rendered by
    // Renderer::drawParticles() as additive-blended billboards. Warm
    // rising embers with alpha fading to 0 as they age.
    {
        auto emitterEntity = app.ecs().createEntity("EmberEmitter");
        if (auto* transform = app.ecs().tryGetComponent<engine::core::Transform>(emitterEntity)) {
            transform->position = {3.0f, 0.1f, -4.0f};
        }
        auto& emitter = app.ecs().addComponent<engine::core::ParticleEmitter>(emitterEntity);
        emitter.settings.looping = true;
        emitter.settings.emissionRate = 30.0f;
        emitter.settings.particleLifetime = 1.8f;
        emitter.settings.particleLifetimeVariance = 0.4f;
        emitter.settings.velocityMin = {-0.3f, 1.2f, -0.3f};
        emitter.settings.velocityMax = {0.3f, 2.5f, 0.3f};
        emitter.settings.gravity = {0.0f, 0.6f, 0.0f}; // slight upward drift -- embers rising, not falling
        emitter.settings.sizeStart = 0.12f;
        emitter.settings.sizeEnd = 0.02f;
        emitter.settings.colorStart = {1.0f, 0.75f, 0.25f, 1.0f};
        emitter.settings.colorEnd = {1.0f, 0.15f, 0.02f, 0.0f};
    }

    // Camera is no longer static -- CharacterController::tick() (run from
    // the pre-tick hook, see Application::initialize()) drives it every
    // frame as a third-person follow camera. This seeds a reasonable
    // starting point for the one frame that can render before the first
    // tick() runs, rather than leaving Camera's arbitrary defaults.
    app.camera().position = {0.0f, 4.0f, -12.0f};
    app.camera().yawDegrees = 90.0f;
    app.camera().pitchDegrees = -15.0f;

    const char* bringUpScript = R"LUA(
        print("engine_runtime: bring-up script started")
        task.spawn(function()
            for i = 1, 3 do
                task.wait(1)
                print("tick from spawned coroutine: " .. i)
            end
        end)
        task.defer(function()
            print("deferred: runs after this resumption cycle")
        end)

        -- world/events smoke test: the real entity/material/physics API
        -- surface (core/ScriptWorldApi.hpp) and the real event bus
        -- (Scripting.hpp's events table), both exercised against the
        -- actual bring-up scene, not a synthetic example.
        local box = world.findByName("DynamicBox")
        if box then
            local x, y, z = world.getPosition(box)
            print(string.format("world.findByName('DynamicBox') -> id=%d at (%.1f, %.1f, %.1f)", box, x, y, z))
            world.setColor(box, 0.2, 0.9, 1.0, 1.0)
            world.setMaterial(box, 0.1, 0.3)
        end

        local updateCount = 0
        events.onUpdate(function(dt)
            updateCount = updateCount + 1
            if updateCount == 1 then
                print("events.onUpdate: first tick, dt=" .. dt)
            end
        end)

        events.onCollision(function(a, b)
            print(string.format("events.onCollision: entity %d touched entity %d", a, b))
        end)

        events.onInteract(function(target, interactor)
            print(string.format("events.onInteract: entity %d interacted with by entity %d", target, interactor))
        end)
    )LUA";
    app.scripting().loadAndRun("BringUpScript", bringUpScript);

    engine::core::logEcsStats(app.ecs());

    // Kronos ("Active Joining UI"): the real Home Screen/Session Browser
    // shell -- only ever constructed for the plain, no-flag launch (see
    // homeScreenMode's own comment above). The bring-up scene above is
    // still built unconditionally either way (zero behavior change for
    // every other real launch mode) -- this just overlays a real menu on
    // top of it and gates mouse capture until the player actually clicks
    // Play/joins a session.
    std::unique_ptr<engine::runtime::RuntimeShell> shell;
    if (homeScreenMode) {
        shell = std::make_unique<engine::runtime::RuntimeShell>(app, [&app, capsuleMesh]() -> engine::core::EntityId {
            // The exact real "networked-player entity" shape the existing
            // --client CLI path already builds above (Transform + Name +
            // Renderable, no physics capsule -- see startNetworking()'s own
            // comment on why) -- reused here as the real callback
            // RuntimeShell::joinSession() calls once a real join actually
            // starts.
            auto networkedPlayer = app.ecs().createEntity("NetworkedPlayer");
            if (auto* transform = app.ecs().tryGetComponent<engine::core::Transform>(networkedPlayer)) {
                transform->position = {2.0f, 3.0f, -6.0f};
            }
            makeRenderable(app.ecs(), networkedPlayer, capsuleMesh, {0.85f, 0.55f, 0.25f}, 0.05f, 0.55f);
            return networkedPlayer;
        }, [&app](glm::vec4 skinTone, engine::core::HeadShape headShape, engine::core::BodyProportions bodyProportions,
                  const engine::core::AvatarLoadout& loadout, const engine::core::CatalogueIndex& catalogueIndex,
                  const engine::core::AnimationOverrides& animationOverrides,
                  engine::core::ClothingFit clothingFit) -> engine::core::EntityId {
            // Kronos ("Avatar System" -- "Spawn a default avatar when
            // entering any world"): the real, same
            // Application::spawnLocalPlayerAvatar() the bring-up world
            // above uses -- one real default avatar spawn path, not two
            // independently-drifting ones. Called here as the real
            // callback RuntimeShell::selectGame() invokes once
            // runtime::loadGame() actually succeeds for a picked
            // ProjectPath Catalogue game. `skinTone`/`headShape`/
            // `bodyProportions`/`loadout`/`catalogueIndex`/
            // `animationOverrides` are the real, chosen (or real, honest
            // default) appearance RuntimeShell resolved from its own
            // core::LocalProfile plus the real, on-disk avatar catalogue/
            // loadout/animation database -- this lambda has no profile/
            // catalogue access of its own, it just forwards what it's
            // given.
            glm::vec3 spawnPosition{0.0f, 3.0f, -6.0f};
            if (!app.spawnLocalPlayerAvatar(spawnPosition, skinTone, headShape, bodyProportions, loadout, catalogueIndex,
                                             animationOverrides, clothingFit)) {
                std::fprintf(stderr, "engine_runtime: spawnLocalPlayerAvatar() failed for a Catalogue game.\n");
                return engine::core::kNullEntity;
            }
            app.characterController().setInitialCameraAngles(-90.0f, -15.0f);
            return app.characterController().entity();
        });
        shell->setTesterSafetyMode(testerSafetyMode);
        if (!shell->initialize()) {
            std::fprintf(stderr, "engine_runtime: RuntimeShell::initialize failed -- continuing without the real "
                                  "Home Screen shell.\n");
            shell.reset();
        } else {
            app.gameLoop()->setPreRenderHook([rawShell = shell.get()](float dt) { rawShell->tick(dt); });
            // Kronos ("Active Joining UI" -- Lua UI bindings): wires
            // ui.sessionBrowser()/ui.playerList()/ui.joinSession(id)/
            // ui.leaveSession() to this real, live shell -- see
            // core::ScriptShellController's own comment. Only ever set
            // when the real shell actually exists; every other CLI mode
            // leaves this nullptr, and the bindings stay real, honest
            // no-ops there.
            app.scriptUiApi().setShellController(shell.get());
        }
    }

    app.run();
    terrain.destroy();
    app.shutdown();
    return 0;
}
