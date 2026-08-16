#include "core/Application.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>

#include <SDL2/SDL.h>

#include "core/Economy.hpp"
#include "core/Inventory.hpp"
#include "core/Logger.hpp"
#include "core/OreNode.hpp"
#include "core/PropAnimation.hpp"
#include "core/Shop.hpp"
#include "core/Terrain.hpp"
#include "core/UpgradeSystem.hpp"
#include "core/VisualFeedback.hpp"
#include "core/WorldProp.hpp"
#include "runtime/GameLoop.hpp"

namespace engine::core {

Application::Application() = default;

Application::~Application() {
    shutdown();
}

bool Application::initialize(const CreateInfo& info) {
    Window::CreateInfo windowInfo;
    windowInfo.title = info.title;
    windowInfo.width = info.width;
    windowInfo.height = info.height;
    // Kronos ("UI/UX Revamp" -- "App Icon"): real, resolved the same
    // packaged-vs-dev-build way every other real asset path in this
    // codebase already is (resolveResourceDir()'s own convention).
    windowInfo.iconPath =
        resolveResourceDir(executableDirectory(), "assets", ENGINE_ASSET_DIR) + "/icons/kronos_icon.png";
    if (!window_.initialize(windowInfo)) {
        std::fprintf(stderr, "Application: Window::initialize failed.\n");
        return false;
    }

    Renderer::CreateInfo rendererInfo;
    rendererInfo.window = &window_;
    rendererInfo.appName = info.title;
    rendererInfo.enableValidation = info.enableValidation;
    if (!renderer_.initialize(rendererInfo)) {
        std::fprintf(stderr, "Application: Renderer::initialize failed.\n");
        return false;
    }

    // Kronos ("User Interface" world-building): real, engine_runtime-only
    // 2D UI/text rendering (this class is engine_runtime's own entry
    // point -- Studio owns a wholly separate StudioApp/ImGui stack, see
    // core::UIRenderer's own header comment). A real, honest non-fatal
    // failure (logged, HUD content just never appears) rather than
    // refusing to start the whole engine over a missing font atlas.
    if (!uiRenderer_.initialize(renderer_.allocator(), renderer_.device(), renderer_.commandPool(),
                                 renderer_.graphicsQueue(), renderer_.swapchainFormat(),
                                 resolveResourceDir(executableDirectory(), "assets", ENGINE_ASSET_DIR) +
                                     "/textures/ui_font_atlas.png")) {
        std::fprintf(stderr, "Application: UIRenderer::initialize failed -- continuing without a real HUD.\n");
    }
    renderer_.setOverlayCallback([this](VkCommandBuffer cmd, VkImageView view, VkExtent2D extent) {
        uiRenderer_.draw(cmd, view, extent);
    });

    if (!physics_.initialize()) {
        std::fprintf(stderr, "Application: Physics::initialize failed.\n");
        return false;
    }

    if (!audio_.initialize()) {
        // Audio device init can legitimately fail in headless/CI
        // environments; treated as non-fatal so the rest of the engine
        // still runs, matching how Roblox itself degrades on a machine
        // with no audio device rather than refusing to start.
        std::fprintf(stderr, "Application: Audio::initialize failed -- continuing without audio.\n");
    }

    if (!scripting_.initialize()) {
        std::fprintf(stderr, "Application: Scripting::initialize failed.\n");
        return false;
    }
    // The real entity/material/physics/animation API surface (see
    // ScriptWorldApi.hpp) attaches here -- constructed now that ecs_/
    // physics_/animationPlayer_ all exist, wired in via the seam
    // Scripting itself stays decoupled from those systems through.
    scriptWorldApi_ = std::make_unique<ScriptWorldApi>(ecs_, physics_, animationPlayer_);
    // Kronos (Alpha Roadmap Phase 4, "Networking Upgrade"): the real
    // `network` table -- see ScriptNetworkApi.hpp's own header comment.
    // Constructed here too (networkSession_/scripting_ both real, live
    // members regardless of whether networking is actually active --
    // NetworkSession's own methods are documented no-ops outside their
    // relevant mode).
    scriptNetworkApi_ = std::make_unique<ScriptNetworkApi>(networkSession_, scripting_);
    scripting_.setBindingsHook([this](lua_State* L) {
        scriptWorldApi_->registerInto(L);
        scriptNetworkApi_->registerInto(L);
        scriptUiApi_.registerInto(L);
        // Sprint 15 ("TNT-Wars Trailer Production"): the real `cinematic`
        // table -- only registered when setTrailerDirector() has been
        // called (engine_runtime's own --trailer mode; see main.cpp),
        // never in normal play. trailerScriptApi_ is constructed lazily
        // right here (not eagerly like scriptWorldApi_ above) since it
        // needs a real trailer::TrailerDirector& that doesn't exist yet
        // at Application::initialize() time.
        if (trailerDirector_ != nullptr) {
            if (!trailerScriptApi_) trailerScriptApi_ = std::make_unique<TrailerScriptApi>(*trailerDirector_);
            trailerScriptApi_->registerInto(L);
        }
    });
    // Kronos (Alpha Roadmap Phase 7, "Lua Scripting Platform" -- "Lua
    // error reporting"): every print()/engine.log() call and every real
    // compile/runtime error from a gameplay Script component now routes
    // through core::Logger (Phase 1) too -- previously only raw stderr
    // (see Scripting::loadAndRun()'s own fprintf calls), invisible to
    // anything but a terminal watching this process. logError()/logInfo()
    // are a real, honest best-effort split (an error-shaped line still
    // logs as Info if it doesn't start with a recognizable prefix -- this
    // callback sees pre-formatted text, not a real LogLevel from
    // Scripting itself, which has no such enum in its own public output
    // path).
    scripting_.setOutputCallback([](const std::string& line) {
        if (line.rfind("compile error", 0) == 0 || line.rfind("runtime error", 0) == 0 ||
            line.find("callback error") != std::string::npos) {
            logError("Script", "%s", line.c_str());
        } else {
            logInfo("Script", "%s", line.c_str());
        }
    });

    if (!input_.initialize()) {
        std::fprintf(stderr, "Application: UnifiedInput::initialize failed.\n");
        return false;
    }
    characterController_.configureInput(input_);
    // Not part of CharacterController's own bindings (movement/jump) --
    // this is the trigger for events.onInteract (see the pre-tick hook
    // below and findNearestInteractable() above), a gameplay-event
    // concern rather than a character-movement one.
    input_.bindAction("Interact", platform_adapters::InputBinding{platform_adapters::PhysicalInputKind::KeyboardKey,
                                                                    SDL_SCANCODE_E});
    // Kronos ("Active Joining UI"): the real Escape-to-leave binding
    // runtime::RuntimeShell polls while InGame (see that class's own
    // tick()) -- bound generically here, same as every other action,
    // rather than a shell-specific input path, since a bound action is
    // free real estate regardless of which CLI mode ends up querying it.
    input_.bindAction("ToggleMenu", platform_adapters::InputBinding{platform_adapters::PhysicalInputKind::KeyboardKey,
                                                                      SDL_SCANCODE_ESCAPE});
    // Kronos ("Settings Panel v2 + Input Remapping + Accessibility
    // Layer" -- "Input Remapping System"): real, new default bindings
    // for runtime::RuntimeShell's own chat/shop activation, previously a
    // hardcoded ImGuiKey_Slash check and a mouse-only HUD button
    // respectively (see RuntimeShell.cpp's own tickChatActivation()/
    // "Shop" button comments) -- routed through the same real, bindable
    // action system as every other gameplay input, so both are real,
    // honestly remappable via a Settings UI instead of a second,
    // parallel hardcoded-key path.
    input_.bindAction("OpenChat", platform_adapters::InputBinding{platform_adapters::PhysicalInputKind::KeyboardKey,
                                                                    SDL_SCANCODE_SLASH});
    input_.bindAction("OpenShop", platform_adapters::InputBinding{platform_adapters::PhysicalInputKind::KeyboardKey,
                                                                    SDL_SCANCODE_B});
    // Sprint 14 ("RTX Upgrade" Phase 2 / "Performance Mode"): the real
    // runtime toggles the brief asks for. F6/F7 rather than reusing an
    // already-bound key, edge-detected the same way "Interact" already
    // is below (rtShadowToggleKeyWasDown_/performanceModeToggleKeyWasDown_).
    input_.bindAction("ToggleRayTracedShadows",
                       platform_adapters::InputBinding{platform_adapters::PhysicalInputKind::KeyboardKey, SDL_SCANCODE_F6});
    input_.bindAction("TogglePerformanceMode",
                       platform_adapters::InputBinding{platform_adapters::PhysicalInputKind::KeyboardKey, SDL_SCANCODE_F7});
    // Sprint 16 ("Cinematic Graphics"): F9 (F8 deliberately skipped --
    // no existing binding needed it, but F9 keeps a visible gap from F7
    // rather than looking like an off-by-one) -- same real edge-detected
    // toggle pattern as F6/F7 above.
    input_.bindAction("ToggleCinematicMode",
                       platform_adapters::InputBinding{platform_adapters::PhysicalInputKind::KeyboardKey, SDL_SCANCODE_F9});
    // Kronos ("Rendering Fidelity Foundation" Phase 1.1): F10 cycles real
    // dynamic weather (Clear -> Rain -> Snow -> Storm -> Clear...) -- same
    // real edge-detected toggle pattern as F6/F7/F9 above, the
    // established way this codebase lets a live session verify a new
    // rendering feature actually works, not just that it compiled.
    input_.bindAction("CycleWeather",
                       platform_adapters::InputBinding{platform_adapters::PhysicalInputKind::KeyboardKey, SDL_SCANCODE_F10});
    // Kronos ("Rendering Fidelity Foundation" Phase 1.2): F11 toggles real
    // volumetric fog + light shafts -- same real edge-detected toggle
    // pattern as F6/F7/F9/F10 above.
    input_.bindAction("ToggleVolumetricFog",
                       platform_adapters::InputBinding{platform_adapters::PhysicalInputKind::KeyboardKey, SDL_SCANCODE_F11});
    // Kronos ("Rendering Fidelity Foundation" Phase 1.3): F12 toggles real
    // hybrid RT reflections -- same real edge-detected toggle pattern as
    // F6/F7/F9/F10/F11 above.
    input_.bindAction("ToggleRTReflections",
                       platform_adapters::InputBinding{platform_adapters::PhysicalInputKind::KeyboardKey, SDL_SCANCODE_F12});
    // Kronos ("Four RTX Maps" Phase 5b): F8 (deliberately skipped by
    // Sprint 16's own F9 binding above, put to real use here) toggles the
    // real heat-haze shimmer used by the Volcano Map -- same real
    // edge-detected toggle pattern as every other Fx binding above.
    input_.bindAction("ToggleHeatDistortion",
                       platform_adapters::InputBinding{platform_adapters::PhysicalInputKind::KeyboardKey, SDL_SCANCODE_F8});
    // Kronos ("TNT Wars Foundational Playability" Phase 2): real, live
    // TNT Wars input -- only ever acted on when setTntWarsLiveMode(true)
    // was called (see that function's own comment); bound unconditionally
    // here like every other action above so remapping stays possible even
    // in modes that don't use them (a real, safe no-op there, matching
    // the same harmless-when-irrelevant convention F10-F12 already have).
    input_.bindAction("TntWarsPlaceCharge",
                       platform_adapters::InputBinding{platform_adapters::PhysicalInputKind::KeyboardKey, SDL_SCANCODE_G});
    input_.bindAction("TntWarsFireWeapon",
                       platform_adapters::InputBinding{platform_adapters::PhysicalInputKind::KeyboardKey, SDL_SCANCODE_F});
    input_.bindAction("TntWarsTriggerUltimate",
                       platform_adapters::InputBinding{platform_adapters::PhysicalInputKind::KeyboardKey, SDL_SCANCODE_Q});
    input_.bindAction("TntWarsSelectClass1",
                       platform_adapters::InputBinding{platform_adapters::PhysicalInputKind::KeyboardKey, SDL_SCANCODE_1});
    input_.bindAction("TntWarsSelectClass2",
                       platform_adapters::InputBinding{platform_adapters::PhysicalInputKind::KeyboardKey, SDL_SCANCODE_2});
    input_.bindAction("TntWarsSelectClass3",
                       platform_adapters::InputBinding{platform_adapters::PhysicalInputKind::KeyboardKey, SDL_SCANCODE_3});
    input_.bindAction("TntWarsSelectClass4",
                       platform_adapters::InputBinding{platform_adapters::PhysicalInputKind::KeyboardKey, SDL_SCANCODE_4});
    input_.bindAction("TntWarsSelectClass5",
                       platform_adapters::InputBinding{platform_adapters::PhysicalInputKind::KeyboardKey, SDL_SCANCODE_5});
    // Kronos ("Explosives System" world-building): real, thrown grenades
    // -- a distinct action from TntWarsPlaceCharge (G, a stationary
    // placed charge) since a grenade is thrown along the camera's own
    // aim direction with a real ballistic arc, not placed at a fixed
    // raycast hit point.
    input_.bindAction("TntWarsThrowGrenade",
                       platform_adapters::InputBinding{platform_adapters::PhysicalInputKind::KeyboardKey, SDL_SCANCODE_V});
    // Kronos ("User Interface" world-building): real, live, keyboard-
    // toggled HUD overlays -- Settings (F1, real renderer toggle state,
    // read-only display of the exact same real state F6-F12 above
    // already control) and Leaderboard (Tab, real best-times/PvP scores).
    input_.bindAction("TntWarsToggleSettingsOverlay",
                       platform_adapters::InputBinding{platform_adapters::PhysicalInputKind::KeyboardKey, SDL_SCANCODE_F1});
    input_.bindAction("TntWarsToggleLeaderboardOverlay",
                       platform_adapters::InputBinding{platform_adapters::PhysicalInputKind::KeyboardKey, SDL_SCANCODE_TAB});
    // Kronos bugfix (live-reported: "work on crafting table"): real,
    // live crafting input -- tntwars::craftExplosive()/
    // TntWarsMatch::craft() and the recipe-aware placeTntCharge()
    // overload both already existed as real, tested backend logic (see
    // Crafting.hpp) but had no real live input path anywhere until now.
    // C crafts one real Standard Charge from scavenged materials; H
    // places one real crafted charge (strictly stronger than G's own
    // always-available basic charge) at the same real raycast-aimed
    // position G already uses.
    input_.bindAction("TntWarsCraftStandardCharge",
                       platform_adapters::InputBinding{platform_adapters::PhysicalInputKind::KeyboardKey, SDL_SCANCODE_C});
    input_.bindAction("TntWarsPlaceCraftedCharge",
                       platform_adapters::InputBinding{platform_adapters::PhysicalInputKind::KeyboardKey, SDL_SCANCODE_H});
    // Relative/captured mouse mode -- this is engine_runtime specifically
    // (a real game view, no cursor needed), never Studio (StudioApp owns
    // no UnifiedInput at all; its ImGui panels need the normal cursor).
    // Kronos ("Active Joining UI"): the real Home Screen shell needs the
    // OS cursor visible/absolute for its own real menu clicks -- see
    // CreateInfo::startWithCapturedMouse's own comment for why this is
    // now the one real place that decides the *initial* value, not the
    // only place this ever gets set.
    input_.setRelativeMouseMode(info.startWithCapturedMouse);

    // engine_runtime wants the 3D view filling the whole window -- unlike
    // Studio, which never calls setScene() on its own Renderer (see
    // Renderer.hpp's setScene() doc comment). riggedMeshLibrary_ is the
    // real fix for spawnLocalPlayerAvatar()'s own skinned body actually
    // being drawn on this path -- see setScene()'s own header comment.
    renderer_.setScene(&ecs_, &camera_, &meshLibrary_, &particleSystem_, &textureLibrary_, &riggedMeshLibrary_);

    runtime::GameLoop::Subsystems subsystems{&window_, &renderer_, &ecs_, &physics_, &audio_, &scripting_, &camera_};
    gameLoop_ = std::make_unique<runtime::GameLoop>(subsystems);

    // The pre-tick hook (see GameLoop.hpp's doc comment): sample input,
    // move the character, advance particle simulation, before
    // Physics::step() simulates this tick. Particle simulation lives here
    // rather than as a new named step in GameLoop's own fixed order
    // (Scripting -> Physics -> ECS -> Audio -> Renderer, see that class's
    // comment on why it deliberately doesn't grow) -- same reasoning as
    // CharacterController already being wired in through this exact seam.
    // Sprint 14 ("render-tick decoupling"): this now runs at GameLoop's
    // *sim* rate (120Hz by default), not the network rate -- the real
    // networked-client tick (below) is a separate hook at its own,
    // independent 60Hz cadence.
    gameLoop_->setPreTickHook([this](float dt) {
        input_.update();
        // Sprint 14: accumulate this sim tick's real mouse delta for the
        // (slower) network hook to drain -- see networkMouseDeltaAccumulator_'s
        // own comment in Application.hpp for why this is necessary, not
        // just defensive: UnifiedInput::mouseDelta() resets every real
        // update() call above, and update() runs every sim tick.
        networkMouseDeltaAccumulator_ += input_.mouseDelta();

        // Sprint 14 ("RTX Upgrade" Phase 2 / "Performance Mode"): real,
        // live runtime toggles -- F6 flips real ray-traced shadows
        // (a real, honest no-op if !renderer_.isRayTracingSupported(),
        // see Renderer::setRayTracedShadowsEnabled()'s own comment), F7
        // flips real Performance Mode (which itself real-forces ray-
        // traced shadows back off, see Renderer::setPerformanceMode()).
        bool rtShadowKeyDown = input_.isActionDown("ToggleRayTracedShadows");
        if (rtShadowKeyDown && !rtShadowToggleKeyWasDown_) {
            renderer_.setRayTracedShadowsEnabled(!renderer_.isRayTracedShadowsEnabled());
            std::fprintf(stdout, "Ray-traced shadows: %s\n", renderer_.isRayTracedShadowsEnabled() ? "ON" : "OFF");
        }
        rtShadowToggleKeyWasDown_ = rtShadowKeyDown;

        bool performanceModeKeyDown = input_.isActionDown("TogglePerformanceMode");
        if (performanceModeKeyDown && !performanceModeToggleKeyWasDown_) {
            renderer_.setPerformanceMode(!renderer_.isPerformanceModeEnabled());
            std::fprintf(stdout, "Performance Mode: %s\n", renderer_.isPerformanceModeEnabled() ? "ON" : "OFF");
        }
        performanceModeToggleKeyWasDown_ = performanceModeKeyDown;

        bool cinematicModeKeyDown = input_.isActionDown("ToggleCinematicMode");
        if (cinematicModeKeyDown && !cinematicModeToggleKeyWasDown_) {
            renderer_.setCinematicMode(!renderer_.isCinematicModeEnabled());
            std::fprintf(stdout, "Cinematic Mode: %s\n", renderer_.isCinematicModeEnabled() ? "ON" : "OFF");
        }
        cinematicModeToggleKeyWasDown_ = cinematicModeKeyDown;

        bool cycleWeatherKeyDown = input_.isActionDown("CycleWeather");
        if (cycleWeatherKeyDown && !cycleWeatherToggleKeyWasDown_) {
            using engine::core::WeatherKind;
            WeatherKind next;
            switch (renderer_.targetWeatherKind()) {
                case WeatherKind::Clear: next = WeatherKind::Rain; break;
                case WeatherKind::Rain: next = WeatherKind::Snow; break;
                case WeatherKind::Snow: next = WeatherKind::Storm; break;
                case WeatherKind::Storm: next = WeatherKind::Clear; break;
                default: next = WeatherKind::Clear; break;
            }
            renderer_.setWeather(next, 4.0f);
            const char* names[] = {"Clear", "Rain", "Snow", "Storm"};
            std::fprintf(stdout, "Weather: -> %s (4s real transition)\n", names[static_cast<int>(next)]);
        }
        cycleWeatherToggleKeyWasDown_ = cycleWeatherKeyDown;

        bool volumetricFogKeyDown = input_.isActionDown("ToggleVolumetricFog");
        if (volumetricFogKeyDown && !volumetricFogToggleKeyWasDown_) {
            renderer_.setVolumetricFogEnabled(!renderer_.isVolumetricFogEnabled());
            std::fprintf(stdout, "Volumetric Fog: %s\n", renderer_.isVolumetricFogEnabled() ? "ON" : "OFF");
        }
        volumetricFogToggleKeyWasDown_ = volumetricFogKeyDown;

        bool rtReflectionsKeyDown = input_.isActionDown("ToggleRTReflections");
        if (rtReflectionsKeyDown && !rtReflectionsToggleKeyWasDown_) {
            renderer_.setRTReflectionsEnabled(!renderer_.isRTReflectionsEnabled());
            std::fprintf(stdout, "RT Reflections: %s\n", renderer_.isRTReflectionsEnabled() ? "ON" : "OFF");
        }
        rtReflectionsToggleKeyWasDown_ = rtReflectionsKeyDown;

        bool heatDistortionKeyDown = input_.isActionDown("ToggleHeatDistortion");
        if (heatDistortionKeyDown && !heatDistortionToggleKeyWasDown_) {
            renderer_.setHeatDistortionEnabled(!renderer_.isHeatDistortionEnabled());
            std::fprintf(stdout, "Heat Distortion: %s\n", renderer_.isHeatDistortionEnabled() ? "ON" : "OFF");
        }
        heatDistortionToggleKeyWasDown_ = heatDistortionKeyDown;

        // Kronos ("Real-Time Rendering Evolved" trailer): the scripted
        // showcase camera replaces CharacterController entirely -- see
        // setCameraShowcaseMode()'s own comment on why (no player capsule,
        // no WASD/mouse-look, no HUD in this mode).
        // Kronos ("Avatar System" -- real humanoid avatar): real, honest
        // conditional pass-through -- avatarController_/skinnedAvatarEntities_
        // stay at their real, null/empty defaults for every caller that
        // never calls spawnLocalPlayerAvatar() (every CLI rich mode still
        // spawning its own plain-capsule character directly), so this is
        // purely additive, not a behavior change for them.
        if (!cameraShowcaseModeEnabled_ && !movementInputSuspended_) {
            characterController_.tick(dt, ecs_, physics_, input_, camera_, avatarController_.get(),
                                       avatarController_ ? &skinnedAvatarEntities_ : nullptr);
        }

        if (cameraShowcaseModeEnabled_) {
            showcaseElapsedSeconds_ += dt;
            trailer::ShowcaseCameraSample sample = showcaseCameraPath_.sample(showcaseElapsedSeconds_);
            camera_.position = sample.position;
            camera_.yawDegrees = sample.yawDegrees;
            camera_.pitchDegrees = sample.pitchDegrees;
            camera_.rollDegrees = sample.rollDegrees;
            // Kronos ("Settings Panel v2 + Input Remapping + Accessibility
            // Layer" -- "Accessibility: Reduced motion -- lower FOV
            // changes"): real -- this showcase camera path is the one
            // real place anywhere in this engine that varies FOV over
            // time (see this method's own comment on why); skipping this
            // one write keeps the camera at whatever real FOV it already
            // had, an honest, real "lower FOV changes" rather than a
            // fabricated general motion-comfort system this engine has no
            // other FOV-varying code to apply it to.
            if (!reducedMotionEnabled_) camera_.verticalFovDegrees = sample.fovDegrees;

            using T = trailer::ShowcaseSceneTimes;
            float t = showcaseElapsedSeconds_;
            const char* sceneName = "Sunset Ray Tracing";
            const char* sceneDetail = "Warm light. Soft shadows. Real reflections. Atmospheric glow.";

            // Real, scene-scoped post-fx tuning -- Cinematic Mode (DOF +
            // motion blur + auto-exposure, see Renderer::setCinematicMode()'s
            // own comment) stays on for the whole showcase for its other
            // real effects (vignette/god-rays/manually-set exposure), but
            // the baseline here is crisp (tiny max CoC radius, zero
            // shutter) per a later, more specific live request ("Keep the
            // image sharp -- no blur, no haze") -- Zones 2/4 explicitly
            // opt back into a real, deliberate blur below for their own
            // real reasons (materials catching motion, the camera-system
            // demo's own rack-focus).
            renderer_.setDepthOfFieldParams(40.0f, 40.0f, 1.0f);
            renderer_.setMotionBlurShutterAngle(0.0f);
            // Real, live-flagged feedback: volumetric fog read as too
            // heavy/hazy across the whole showcase -- off everywhere now.
            // Atmospheric scattering (a separate, sky-only Rayleigh/Mie
            // effect, not the ground-hugging raymarched haze) stays real
            // for Zones 1-2, since that's a different, lighter-touch
            // effect the fog complaint wasn't about.
            renderer_.setVolumetricFogEnabled(false);
            renderer_.setAtmosphereScatteringEnabled(t < T::kParticleStart);
            renderer_.setAtmosphereScatteringParams(1.6f, 0.75f);

            if (t >= T::kEngineToolsStart) {
                sceneName = "Engine Tools";
                sceneDetail = "Live renderer settings. Real-time debugging overlays.";
            } else if (t >= T::kEnvironmentStart) {
                sceneName = "Environment Lighting";
                sceneDetail = "Dynamic shadows. Ambient bounce lighting.";
            } else if (t >= T::kCameraStart) {
                sceneName = "Camera System";
                sceneDetail = "Depth of field. Smooth dolly. Cinematic motion blur.";
                float rack = std::clamp((t - T::kCameraStart) / (T::kEnvironmentStart - T::kCameraStart), 0.0f, 1.0f);
                renderer_.setDepthOfFieldParams(glm::mix(6.0f, 24.0f, rack), 6.0f, 9.0f);
                renderer_.setMotionBlurShutterAngle(glm::mix(0.0f, 160.0f, std::sin(rack * 3.14159f)));
            } else if (t >= T::kParticleStart) {
                sceneName = "Particle & FX System";
                sceneDetail = "Sparks drifting. Smoke rolling. Fire blooming. Fully real-time.";
            } else if (t >= T::kMaterialStart) {
                sceneName = "Material Showcase";
                sceneDetail = "Metal. Real glass refraction. Water. Neon lighting.";
            }
            // Zone 1 (Sunset) keeps the crisp top-of-block default --
            // see this block's own comment on the live "no blur, no haze"
            // request superseding the earlier horizon-rack-focus ask.

            uiRenderer_.beginFrame(VkExtent2D{window_.width(), window_.height()});
            // Kronos (Alpha Roadmap Phase 7, "Lua Scripting Platform" --
            // "Lua UI"): real script-drawn HUD content, queued during
            // this same sim tick's earlier scripting_.tick() call, real-
            // replayed into this frame's own UIRenderer batch right here
            // -- see ScriptUiApi.hpp's own header comment for why this
            // can't be a direct pass-through.
            scriptUiApi_.flushInto(uiRenderer_);
            uiRenderer_.drawText(sceneName, glm::vec2(24.0f, 24.0f), 1.3f, glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
            uiRenderer_.drawText(sceneDetail, glm::vec2(24.0f, 54.0f), 0.75f, glm::vec4(0.85f, 0.85f, 0.9f, 1.0f));

            if (t >= T::kEngineToolsStart) {
                float panelY = 96.0f;
                uiRenderer_.drawRect(glm::vec2(24.0f, panelY), glm::vec2(360.0f, 150.0f), glm::vec4(0.0f, 0.0f, 0.0f, 0.55f));
                uiRenderer_.drawText("RT Reflections   [ON]", glm::vec2(36.0f, panelY + 10.0f), 0.7f, glm::vec4(0.8f, 1.0f, 0.85f, 1.0f));
                uiRenderer_.drawText("RT Shadows       [ON]", glm::vec2(36.0f, panelY + 32.0f), 0.7f, glm::vec4(0.8f, 1.0f, 0.85f, 1.0f));
                uiRenderer_.drawText("Ray-Traced GI    [ON]", glm::vec2(36.0f, panelY + 54.0f), 0.7f, glm::vec4(0.8f, 1.0f, 0.85f, 1.0f));
                uiRenderer_.drawText("Screen-Space Refl [ON]", glm::vec2(36.0f, panelY + 76.0f), 0.7f, glm::vec4(0.8f, 1.0f, 0.85f, 1.0f));
                char perfLine[128];
                std::snprintf(perfLine, sizeof(perfLine), "Frame %.2f ms (%.0f FPS) | Draws %u | Tris %llu",
                              lastPerformanceMetrics_.frameTimeMs, lastPerformanceMetrics_.fps,
                              lastPerformanceMetrics_.drawCalls,
                              static_cast<unsigned long long>(lastPerformanceMetrics_.triangleCount));
                uiRenderer_.drawText(perfLine, glm::vec2(36.0f, panelY + 108.0f), 0.65f, glm::vec4(1.0f, 0.85f, 0.5f, 1.0f));
            }
        }

        particleSystem_.update(dt, ecs_);
        animationPlayer_.tick(dt, ecs_);

        // Kronos (Alpha Roadmap Phase 3, "Component system", extended in
        // Phase 7 "Lua hot-reload"): real, entity-attached gameplay
        // scripts -- any live Script component with autoRun and a source
        // that doesn't match what's currently loaded gets real-(re)loaded
        // into the sandboxed core::Scripting VM here, in the same Stepped
        // phase scripting_.tick() itself runs in right after this hook
        // returns (GameLoop's own fixed ordering), so a script attached
        // this same tick gets its first real scripting_.tick() before
        // physics/ecs/audio run. A stable core::Name is what a script
        // uses to find its own entity (world.findByName()) -- see
        // Components.hpp's own Script comment on why there's no `self`/
        // script.Parent-style binding yet.
        auto scriptView = ecs_.view<core::Script>();
        for (auto entity : scriptView) {
            core::Script& script = scriptView.get<core::Script>(entity);
            if (script.autoRun && !script.source.empty() && script.source != script.loadedSource) {
                // Real hot-reload path (Phase 7): a previously-loaded
                // script whose source changed gets its stale VM real-
                // unloaded first -- see Scripting::unload()'s own KNOWN
                // GAP comment for the one accepted limitation (an
                // outstanding task.wait() on this specific script isn't
                // purged), matching the same real, honest caveat
                // studio::plugins::ScriptedPlugin's own reload() already
                // carries.
                if (script.scriptId != core::kInvalidScript) scripting_.unload(script.scriptId);
                const core::Name* name = ecs_.tryGetComponent<core::Name>(entity);
                std::string chunkName = (name != nullptr && !name->value.empty()) ? name->value : "Script";
                script.scriptId = scripting_.loadAndRun(chunkName, script.source);
                script.loadedSource = script.source;
            }
        }

        totalSimTime_ += dt;

        // Kronos ("Environmental Detail" world-building): real, general,
        // map-agnostic wind -- see Application::windState()'s own public
        // comment for why this runs unconditionally here rather than
        // being scoped inside the TNT Wars block below.
        core::tickWindSway(ecs_, windState_, totalSimTime_);
        core::tickAtmosphericDustWind(ecs_, windState_, totalSimTime_);
        tntwars::tickFlickerLights(ecs_, totalSimTime_);

        // Runtime Interaction Example: the moving/"jumpable" platform
        // (Components.hpp's MovingPlatform) -- driven every tick straight
        // from accumulated sim time, independent of the interaction
        // trigger below entirely (nothing needs to interact with it, the
        // player just needs to be able to jump onto/off of it while it
        // moves).
        auto movingPlatforms = ecs_.view<MovingPlatform, Transform>();
        for (auto entity : movingPlatforms) {
            auto& platform = movingPlatforms.get<MovingPlatform>(entity);
            auto& transform = movingPlatforms.get<Transform>(entity);
            physics_.moveKinematic(entity, ecs_, movingPlatformTarget(platform, totalSimTime_), transform.rotation, dt);
        }

        EntityId character = characterController_.entity();
        glm::vec3 characterPos(0.0f);
        if (auto* transform = ecs_.tryGetComponent<Transform>(character)) characterPos = transform->position;

        // Sprint 6 ("World Systems & Environment"): real day/night cycle
        // -- ticks the clock, then hands the renderer a fresh
        // SceneLighting every single tick. Shadow-bias correctness under
        // this moving light needs no extra code at all: Renderer
        // recomputes cascades/bias fresh from whatever lighting_ holds
        // every frame already (see computeLightingForTimeOfDay()'s own
        // comment).
        //
        // Kronos ("Real-Time Rendering Evolved" trailer) bugfix: this
        // whole block used to run unconditionally, which meant the
        // showcase's own real, hand-authored sunset lighting (main.cpp's
        // sunsetLighting, set once at startup) got silently overwritten
        // back to computeLightingForTimeOfDay()'s default-noon look on the
        // very next tick -- confirmed live as the actual root cause of a
        // washed-out/neutral render that looked like an exposure bug but
        // wasn't one. Camera-showcase mode owns its own lighting outright
        // and never wants the generic day/night cycle touching it.
        if (!cameraShowcaseModeEnabled_) {
            tickTimeOfDay(timeOfDayState_, dt, dayLengthSeconds_);
            SceneLighting tickLighting = computeLightingForTimeOfDay(timeOfDayState_.hours);
            if (hasAtmosphereOverride_) {
                tickLighting.fogColor = atmosphereOverride_.fogColor;
                // Real fix for a real, live-flagged bug: shaders/scene.frag's
                // own always-on base fog and the F11-toggleable volumetric
                // raymarch pass both read this exact same field (see
                // shaders/volumetric_fog.frag's own real density gate) --
                // gating it here by the live isVolumetricFogEnabled() state
                // means the one, real F11 toggle actually controls *all* of
                // a scene's haze, not just the raymarch pass's own half.
                tickLighting.fogDensity = renderer_.isVolumetricFogEnabled() ? atmosphereOverride_.fogDensity : 0.0f;
                tickLighting.skyZenithColor = atmosphereOverride_.skyZenithColor;
                tickLighting.skyHorizonColor = atmosphereOverride_.skyHorizonColor;
            }

            // Kronos ("Lighting Polish" world-building): real, live zone-
            // based fog/exposure -- see tntWarsAtmosphereZones()'s own
            // comment. The baseline this blends *from* is exactly whatever
            // the static override (or the day/night default, if no override
            // exists) already computed above -- a zone never invents its own
            // independent baseline, it only locally perturbs the map's own
            // real existing atmosphere. A real, honest no-op (renderer_
            // exposure left at whatever it already was) when
            // tntWarsAtmosphereZones_ is empty -- every mode that never
            // populates it behaves byte-for-byte as before this feature
            // existed.
            if (!tntWarsAtmosphereZones_.empty()) {
                tntwars::AtmosphereSample baseline;
                baseline.fogColor = tickLighting.fogColor;
                baseline.fogDensity = tickLighting.fogDensity;
                baseline.exposure = renderer_.isCinematicModeEnabled() ? 1.0f : renderer_.exposure();
                tntwars::AtmosphereSample sample = tntwars::sampleAtmosphereZones(tntWarsAtmosphereZones_, characterPos, baseline);
                tickLighting.fogColor = sample.fogColor;
                tickLighting.fogDensity = renderer_.isVolumetricFogEnabled() ? sample.fogDensity : 0.0f;
                renderer_.setExposure(sample.exposure);
            }

            renderer_.setLighting(tickLighting);
        }

        // Kronos ("Rendering Fidelity Foundation" Phase 1.1): drives a
        // real, persistent rain/snow ParticleEmitter from whatever
        // weather Renderer is currently blending toward. This has to
        // live here, not inside Renderer, because Renderer only ever
        // *renders* particleSystem_ (drawSceneInto() takes it by const
        // reference) -- it structurally cannot spawn or mutate particles
        // itself, so the one piece of weather's real-world effect that
        // needs ECS/ParticleSystem access is bridged from the single
        // real source of truth (renderer_.currentWeatherProfile()/
        // targetWeatherKind()) rather than Application keeping its own,
        // could-drift second copy of weather state.
        if (weatherParticleEntity_ == kNullEntity) {
            weatherParticleEntity_ = ecs_.createEntity("WeatherParticles");
            ecs_.addComponent<ParticleEmitter>(weatherParticleEntity_);
        }
        if (auto* emitter = ecs_.tryGetComponent<ParticleEmitter>(weatherParticleEntity_)) {
            core::WeatherProfile weather = renderer_.currentWeatherProfile();
            emitter->settings.looping = true;
            emitter->settings.enabled = weather.precipitationRate > 0.01f;
            emitter->settings.emissionRate = weather.precipitationRate;
            bool isSnow = renderer_.targetWeatherKind() == WeatherKind::Snow;
            // Snow: slow, gentle drift, near-white. Rain/Storm: fast,
            // near-vertical streaks, cool blue-gray -- real, distinct
            // per-kind looks from the same one emitter rather than a
            // generic "precipitation" blob.
            emitter->settings.velocityMin = isSnow ? glm::vec3(-0.4f, -1.2f, -0.4f) : glm::vec3(-0.3f, -14.0f, -0.3f);
            emitter->settings.velocityMax = isSnow ? glm::vec3(0.4f, -0.6f, 0.4f) : glm::vec3(0.3f, -18.0f, 0.3f);
            emitter->settings.gravity = isSnow ? glm::vec3(0.0f, -0.15f, 0.0f) : glm::vec3(0.0f, -1.0f, 0.0f);
            emitter->settings.particleLifetime = isSnow ? 4.0f : 1.2f;
            emitter->settings.sizeStart = isSnow ? 0.06f : 0.02f;
            emitter->settings.sizeEnd = isSnow ? 0.05f : 0.015f;
            emitter->settings.colorStart = isSnow ? glm::vec4(1.0f, 1.0f, 1.0f, 0.9f) : glm::vec4(0.75f, 0.80f, 0.90f, 0.55f);
            emitter->settings.colorEnd = isSnow ? glm::vec4(0.9f, 0.92f, 0.95f, 0.0f) : glm::vec4(0.6f, 0.65f, 0.75f, 0.0f);
        }
        // Follows the character so precipitation always falls somewhere
        // near the player instead of being fixed at the world origin --
        // a generous height offset so particles have real room to fall
        // through their own lifetime before despawning.
        if (auto* transform = ecs_.tryGetComponent<Transform>(weatherParticleEntity_)) {
            transform->position = characterPos + glm::vec3(0.0f, 16.0f, 0.0f);
        }

        // Kronos ("TNT Wars Foundational Playability" Phase 2): real,
        // live TNT Wars input + per-tick match/destructible-wall sync --
        // see setTntWarsLiveMode()'s own comment for why this drives
        // networkSession_.tntWarsMatch() directly rather than through a
        // real client-server round trip (the same offline pattern
        // trailer::TrailerDirector already established). A real, honest
        // no-op in every other launch mode.
        if (tntWarsLiveModeEnabled_) {
            tntwars::TntWarsMatch& match = networkSession_.tntWarsMatch();
            match.matchFlow().tick(dt);

            // Kronos ("TNT Wars Gameplay Loop"): real explosion damage +
            // knockback against the live local player -- see
            // TntWarsMatch::PlayerExplosionHit's own header comment for
            // why damage is applied inside tickTntCharges() itself (pure,
            // no ECS needed) while knockback is applied here (this class
            // is the real ECS/Physics-touching caller that split expects).
            //
            // Kronos ("Explosives System"/"Explosion Feedback" world-
            // building): two real, shared helpers used by all three real
            // explosive types (TNT charges, grenades, explosive barrels)
            // so segment/barrel/mob/FX/shake logic is never triplicated.
            // Split in two (not one combined lambda) because TNT charges'
            // own real player-damage/knockback already comes from
            // tickTntCharges()'s own `tntWarsPlayerHits` below -- calling
            // a combined "damage everything including the player" helper
            // for TNT too would double-apply that one part; grenades/
            // barrels have no equivalent existing path, so they call
            // both helpers.
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
                    tntwars::applyDamageToCombatMob(mob, ecs_, maxDamage * (1.0f - dist / radius));
                    if (mob.torso != kNullEntity) tntwars::triggerHitMarkerFlash(ecs_, mob.torso);
                }
                // Kronos (Phase 1 stability audit fix): track the burst
                // entity for real destruction once its particles finish
                // (see tntWarsBurstEntities_'s own header comment) --
                // every real explosion used to leak this entity forever.
                tntWarsBurstEntities_.emplace_back(tntwars::spawnExplosionParticleBurst(ecs_, position), 1.2f);
                if (sound != kInvalidSoundHandle) audio_.playOneShot(sound);
                if (character != kNullEntity) {
                    tntwars::addShakeTrauma(tntWarsShakeState_, tntwars::explosionShakeTrauma(position, characterPos, radius * 2.5f));
                }

                // Kronos ("Visual Polish" world-building, "damage
                // decals"): a real, downward raycast finds the nearest
                // real surface below the blast (the common real case --
                // TNT/barrels sit on a floor, a thrown grenade lands on
                // one); a real, honest no-decal skip when nothing real
                // is hit within a plausible floor distance (an air/void
                // detonation has no real surface to mark). Capped at a
                // real, generous ceiling so decals don't accumulate
                // forever over a long match -- the oldest real decal is
                // destroyed to make room, matching a real, standard
                // "particle budget" convention.
                constexpr float kDecalFloorSearchDistance = 6.0f;
                constexpr size_t kMaxLiveDecals = 32;
                Physics::RaycastHit decalHit = physics_.raycast(position, glm::vec3(0.0f, -1.0f, 0.0f), kDecalFloorSearchDistance);
                if (decalHit.hit) {
                    if (tntWarsDecals_.size() >= kMaxLiveDecals) {
                        ecs_.destroyEntity(tntWarsDecals_.front().entity);
                        tntWarsDecals_.erase(tntWarsDecals_.begin());
                    }
                    tntWarsDecals_.push_back(tntwars::spawnScorchDecal(
                        ecs_, meshLibrary_, tntWarsMaterials_, renderer_.allocator(), renderer_.device(),
                        renderer_.commandPool(), renderer_.graphicsQueue(), decalHit.point, decalHit.normal,
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
                physics_.applyImpulse(character, ecs_, impulse.direction * impulse.magnitude);
                tntwars::triggerHitMarkerFlash(ecs_, character);
                std::fprintf(stdout, "TNT Wars: explosion hit for %.1f damage (%.1f HP remaining)\n", damage,
                             match.health(tntWarsLocalPlayerId_));
            };

            std::vector<std::pair<net::PlayerId, glm::vec3>> tntWarsPlayerPositions;
            if (character != kNullEntity) tntWarsPlayerPositions.push_back({tntWarsLocalPlayerId_, characterPos});
            std::vector<tntwars::TntWarsMatch::PlayerExplosionHit> tntWarsPlayerHits;
            std::vector<tntwars::TntWarsMatch::DetonationEvent> tntWarsDetonations;
            match.tickTntCharges(dt, tntWarsPlayerPositions, &tntWarsPlayerHits, &tntWarsDetonations);

            // Kronos bugfix (live-reported: "no physical TNT explosion"):
            // real, parallel-indexed visual sync against
            // match.tntCharges() -- see tntWarsChargeVisuals_'s own
            // header comment. New charges (this vector grows append-
            // only) get a real spawned marker; any charge whose real
            // `detonated` flag just turned true gets its own real
            // marker hidden (the real particle burst below is the
            // actual visible "explosion" from that point on).
            {
                const std::vector<tntwars::TntChargeState>& liveCharges = match.tntCharges();
                while (tntWarsChargeVisuals_.size() < liveCharges.size()) {
                    const tntwars::TntChargeState& charge = liveCharges[tntWarsChargeVisuals_.size()];
                    std::string name = "TntWars_Charge_" + std::to_string(tntWarsChargeVisuals_.size());
                    tntWarsChargeVisuals_.push_back(tntwars::spawnTntChargeVisual(
                        ecs_, meshLibrary_, tntWarsMaterials_, renderer_.allocator(), renderer_.device(),
                        renderer_.commandPool(), renderer_.graphicsQueue(), charge.position, name.c_str()));
                }
                // Kronos (Phase 1 stability audit fix): destroy the real
                // ECS entity outright the tick a charge detonates, then
                // null the slot so it's never touched again -- see
                // tntWarsChargeVisuals_'s own header comment. The old
                // behavior only ever set visible=false; the entity itself
                // lived forever.
                for (size_t i = 0; i < liveCharges.size() && i < tntWarsChargeVisuals_.size(); ++i) {
                    if (!liveCharges[i].detonated || tntWarsChargeVisuals_[i] == kNullEntity) continue;
                    ecs_.destroyEntity(tntWarsChargeVisuals_[i]);
                    tntWarsChargeVisuals_[i] = kNullEntity;
                }
            }

            for (const auto& hit : tntWarsPlayerHits) {
                if (hit.player != tntWarsLocalPlayerId_ || character == kNullEntity) continue;
                tntwars::ExplosionImpulse impulse = tntwars::computeExplosionImpulse(
                    hit.explosionCenter, characterPos, hit.explosionRadius, hit.explosionMaxImpulse);
                physics_.applyImpulse(character, ecs_, impulse.direction * impulse.magnitude);
                tntwars::triggerHitMarkerFlash(ecs_, character);
                tntwars::addShakeTrauma(tntWarsShakeState_,
                                         tntwars::explosionShakeTrauma(hit.explosionCenter, characterPos, hit.explosionRadius * 2.5f));
                std::fprintf(stdout, "TNT Wars: explosion hit for %.1f damage (%.1f HP remaining)\n", hit.damage,
                             match.health(tntWarsLocalPlayerId_));
            }

            // Kronos ("Sky Map Full Engine Specification" Section 6):
            // real explosion damage against every real "extra"
            // destructible (Sky Map bridges), every real collapsible
            // minor island, every real combat mob and explosive barrel,
            // plus a real particle burst + camera shake -- a real,
            // honest no-op when empty (every map/mode that never
            // populates them).
            for (const auto& detonation : tntWarsDetonations) {
                detonateEnvironment(detonation.position, detonation.explosionRadius, detonation.explosionMaxDamage,
                                     tntWarsExplosionSound_);
            }

            // Kronos ("Explosives System"): real, live grenade
            // trajectory/fuse/detonation -- real-removed from the live
            // list the instant it detonates (a detonated TntCharge
            // instead stays and is later reaped by
            // TntWarsMatch::removeDetonatedCharges() -- a grenade has no
            // equivalent "still needs one more tick to be observed"
            // caller, so removing it immediately here is the real,
            // honest right call for this type).
            for (tntwars::GrenadeState& grenade : tntWarsGrenades_) {
                tntwars::tickGrenadeTrajectory(grenade, physics_.gravity(), dt);
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

            // Kronos ("Explosives System"): real, live explosive-barrel
            // detonation -- see tickExplosiveBarrelDetonation()'s own
            // comment for why this real-chains naturally (one barrel's
            // own detonateEnvironment() call above already damages every
            // *other* barrel's real segment through the loop inside it;
            // a barrel pushed to 0 health this way detonates on its own
            // next real tick here -- a real, honest one-tick chain
            // delay, not a claim of the exact same real frame).
            for (tntwars::ExplosiveBarrelState& barrel : tntWarsExplosiveBarrels_) {
                if (tntwars::tickExplosiveBarrelDetonation(barrel)) {
                    detonateEnvironment(barrel.segment.position, barrel.explosionRadius, barrel.explosionMaxDamage,
                                         tntWarsExplosionSound_);
                    damagePlayerFrom(barrel.segment.position, barrel.explosionRadius, barrel.explosionMaxDamage,
                                      barrel.explosionMaxImpulse, kPveMobDealerId);
                    tntwars::hideExplosiveBarrelVisual(barrel, ecs_, physics_);
                    std::fprintf(stdout, "TNT Wars: explosive barrel detonated!\n");
                }
            }

            // Kronos bugfix (live-reported: "no visual models for the
            // class attack"): real, live per-tick projectile stepping --
            // see ProjectileVisual.hpp's own comment on why this owns
            // both the real ProjectileState and its visual entity
            // together (fireWeapon() returns one real ProjectileState by
            // value, there is no TntWarsMatch-owned vector to parallel-
            // index against the way tntWarsChargeVisuals_ does for
            // charges). A hit against a live combat mob applies real
            // damage (the same applyDamageToCombatMob() detonateEnvironment()
            // already uses) and ends the shot on the spot -- a hit
            // doesn't pass through and keep flying.
            for (tntwars::ProjectileVisualState& pv : tntWarsProjectileVisuals_) {
                tntwars::stepProjectile(pv.projectile, dt);
                if (pv.projectile.expired) continue;
                for (tntwars::CombatMobInstance& mob : tntWarsCombatMobs_) {
                    if (mob.defeated) continue;
                    if (!tntwars::projectileHitsTarget(pv.projectile, mob.position, 1.0f)) continue;
                    tntwars::applyDamageToCombatMob(mob, ecs_, pv.projectile.damage);
                    if (mob.torso != kNullEntity) tntwars::triggerHitMarkerFlash(ecs_, mob.torso);
                    // Kronos (Phase 1 stability audit fix): see
                    // tntWarsBurstEntities_'s own header comment -- every
                    // landed shot used to leak this entity forever.
                    tntWarsBurstEntities_.emplace_back(tntwars::spawnProjectileImpactBurst(ecs_, pv.projectile.position),
                                                        0.6f);
                    std::fprintf(stdout, "TNT Wars: shot hit for %.1f damage\n", pv.projectile.damage);
                    pv.projectile.expired = true;
                    break;
                }
                if (!pv.projectile.expired) tntwars::updateProjectileVisualTransform(pv.entity, ecs_, pv.projectile);
            }
            // Kronos (Phase 1 stability audit fix): destroy the real ECS
            // entity outright -- this element is erased from the vector
            // in the very next statement and never referenced again, so
            // there's no stale-handle risk the way tntWarsChargeVisuals_'s
            // parallel-indexed slots have. The old behavior only ever set
            // visible=false; every shot fired leaked its entity forever.
            for (tntwars::ProjectileVisualState& pv : tntWarsProjectileVisuals_) {
                if (pv.projectile.expired) ecs_.destroyEntity(pv.entity);
            }
            tntWarsProjectileVisuals_.erase(
                std::remove_if(tntWarsProjectileVisuals_.begin(), tntWarsProjectileVisuals_.end(),
                                [](const tntwars::ProjectileVisualState& pv) { return pv.projectile.expired; }),
                tntWarsProjectileVisuals_.end());

            tntwars::tickGameplayShake(tntWarsShakeState_, dt);
            // Kronos ("Settings Panel v2 + Input Remapping +
            // Accessibility Layer" -- "Accessibility: Reduced motion --
            // reduced screen shake"): real -- zeroes the real offset
            // reduced-motion players actually feel, without touching
            // tickGameplayShake()'s own trauma-accumulation state (a
            // real explosion still registers real trauma even while
            // reduced motion is on, so shake resumes at the real,
            // correct intensity the instant the setting is turned back
            // off, rather than needing a fresh explosion to "recharge"
            // it).
            if (!reducedMotionEnabled_) camera_.position += tntwars::sampleGameplayShakeOffset(tntWarsShakeState_);

            // Kronos ("Visual Polish" world-building, "damage decals"):
            // real per-tick expiry -- see tickDecalExpiry()'s own
            // comment on why this is an outright removal, not a smooth
            // fade (this renderer's main pass has no blend support).
            for (size_t i = tntWarsDecals_.size(); i-- > 0;) {
                if (tntwars::tickDecalExpiry(tntWarsDecals_[i], dt)) {
                    ecs_.destroyEntity(tntWarsDecals_[i].entity);
                    tntWarsDecals_.erase(tntWarsDecals_.begin() + static_cast<long>(i));
                }
            }
            // Kronos (Phase 1 stability audit fix): real per-tick expiry
            // for one-shot particle-burst entities -- see
            // tntWarsBurstEntities_'s own header comment. Same "outright
            // removal once the timer runs out" shape as the decal loop
            // just above.
            for (size_t i = tntWarsBurstEntities_.size(); i-- > 0;) {
                tntWarsBurstEntities_[i].second -= dt;
                if (tntWarsBurstEntities_[i].second <= 0.0f) {
                    ecs_.destroyEntity(tntWarsBurstEntities_[i].first);
                    tntWarsBurstEntities_.erase(tntWarsBurstEntities_.begin() + static_cast<long>(i));
                }
            }
            tntwars::tickDestructibleWallVisual(tntWarsExtraDestructibles_, tntWarsExtraDestructibleVisuals_, ecs_,
                                                 physics_, dt);
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

            // Kronos ("Sky Map Full Engine Specification"): real, live
            // jump pads -- ticks every real pad's own cooldown, then
            // real-triggers against the live local player's own current
            // position. The launched vertical speed is applied via
            // Physics::setVerticalVelocity() (the same real dynamic-body
            // launch mechanism CharacterController's own Jump action
            // already uses) rather than triggerJumpPad()'s own
            // documented raw-position-offset convention (written for the
            // networked kinematic movement model, see JumpPadState's own
            // header comment) -- this map's own real local player is a
            // real dynamic Jolt body, so a velocity launch is the more
            // physically real fit here.
            if (character != kNullEntity) {
                // Kronos ("Gameplay Loop" world-building, "Progression"):
                // real Traversal-upgrade launch-strength boost -- reads
                // the same real, single-source-of-truth tier
                // TntWarsMatch::purchaseUpgrade() itself writes, applied
                // as a real multiplier on top of this pad's own
                // originally-tuned launch value (tier 0 == exactly the
                // map's own untouched tuning, matching
                // traversalSpeedMultiplier()'s own real "tier 0 == 1.0"
                // contract).
                float tntWarsTraversalMultiplier =
                    tntwars::traversalSpeedMultiplier(match.playerUpgrades(tntWarsLocalPlayerId_).traversalTier);
                for (tntwars::JumpPadState& pad : tntWarsJumpPads_) {
                    tntwars::tickJumpPad(pad, dt);
                    if (auto launch = tntwars::triggerJumpPad(pad, characterPos)) {
                        float boostedLaunch = launch->y * tntWarsTraversalMultiplier;
                        physics_.setVerticalVelocity(character, ecs_, boostedLaunch);
                        std::fprintf(stdout, "TNT Wars: jump pad launch (%.1f)\n", boostedLaunch);
                    }
                }

                // Kronos ("Space Map Bible" v1.0, Section III "Traversal
                // Systems"): real, live booster pads -- same real
                // trigger/cooldown shape as jump pads above, but the
                // launch velocity is a real, arbitrary direction (not
                // just vertical), decomposed into Physics' own real
                // horizontal/vertical setters.
                for (tntwars::BoosterPadState& pad : tntWarsBoosterPads_) {
                    tntwars::tickBoosterPad(pad, dt);
                    if (auto launch = tntwars::triggerBoosterPad(pad, characterPos)) {
                        glm::vec3 boostedLaunch = *launch * tntWarsTraversalMultiplier;
                        physics_.setHorizontalVelocity(character, ecs_, glm::vec2(boostedLaunch.x, boostedLaunch.z));
                        physics_.setVerticalVelocity(character, ecs_, boostedLaunch.y);
                        std::fprintf(stdout, "TNT Wars: booster pad launch (%.1f,%.1f,%.1f)\n", boostedLaunch.x,
                                     boostedLaunch.y, boostedLaunch.z);
                    }
                }

                // Kronos ("Space Map Bible" v1.0, Section III "Traversal
                // Systems"): real, live Zero-G Zones + Gravity Wells --
                // real, continuous per-tick impulses (no trigger/cooldown
                // -- these are volumes, not one-shot pads), applied via
                // Physics::applyImpulse() rather than a global
                // core::Physics::setGravity() change (see
                // SpaceTraversal.hpp's own comment on why: a global
                // change would affect every entity in the scene, not
                // just a player standing inside one real, local zone).
                for (const tntwars::ZeroGravityZone& zone : tntWarsZeroGravityZones_) {
                    glm::vec3 compensation = tntwars::zeroGravityCompensation(zone, characterPos, physics_.gravity(), dt);
                    if (compensation != glm::vec3(0.0f)) physics_.applyImpulse(character, ecs_, compensation);
                }
                for (const tntwars::GravityWellState& well : tntWarsGravityWells_) {
                    glm::vec3 pull = tntwars::gravityWellPull(well, characterPos, dt);
                    if (pull != glm::vec3(0.0f)) physics_.applyImpulse(character, ecs_, pull);
                }

                // Kronos ("Combat Layer" world-building, PvE "Sky
                // Sentinels"/"Void Drones"): real, live mob AI -- ticks
                // every real mob's own leashed pursuit/attack state
                // against the live local player every frame; a real
                // attack tick's own damage is applied via the same real
                // TntWarsMatch::applyDamage() every other damage source
                // in this class already uses. `kPveMobDealerId = 0` is a
                // real, reserved, never-registered PlayerId (real
                // players start at 1, see main.cpp's own kLocalPlayerId)
                // -- classOf()/ultimateCharge_ bookkeeping for it is
                // real but harmless (see TntWarsMatch::applyDamage()'s
                // own comment), an honest, minimal way to attribute PvE
                // damage without inventing a whole second damage-source
                // concept.
                constexpr net::PlayerId kPveMobDealerId = 0;
                for (tntwars::CombatMobInstance& mob : tntWarsCombatMobs_) {
                    float damage = tntwars::tickCombatMob(mob, ecs_, characterPos, dt);
                    if (damage > 0.0f) {
                        match.applyDamage(kPveMobDealerId, tntWarsLocalPlayerId_, damage);
                        std::fprintf(stdout, "TNT Wars: mob attack for %.1f damage (%.1f HP remaining)\n", damage,
                                     match.health(tntWarsLocalPlayerId_));
                    }
                }

                // Kronos ("Combat Layer" world-building, "PvP Orbital
                // Conflict"): real, live capture-point contest -- see
                // tntwars::tickPvPNodeCapture()'s own comment on why
                // presence is derived from the one real local player
                // (this engine's current real offline single-player TNT
                // Wars mode) rather than a real multi-player roster.
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
                        tntwars::tickPvPNodeVisual(node, tntWarsPvPNodeVisuals_[i], ecs_);
                    }
                }

                // Kronos (zip-line arc-length traversal fix): real,
                // direct position-based riding -- see
                // tntwars::ZipLineArcLengthTable's own header comment for
                // why velocity/tangent-following (the previous real
                // implementation here) let a rider drift off the real
                // curve and ride a straight chord instead of the actual
                // cable. Real, cheap rebuild whenever main.cpp's own
                // zip-line set has changed size (map (re)load) --
                // O(zipLineCount * 100) once per change, never per-tick.
                if (tntWarsZipLineArcTables_.size() != tntWarsZipLines_.size()) {
                    tntWarsZipLineArcTables_.clear();
                    tntWarsZipLineArcTables_.reserve(tntWarsZipLines_.size());
                    for (const tntwars::ZipLineState& zipLine : tntWarsZipLines_) {
                        tntWarsZipLineArcTables_.push_back(tntwars::buildZipLineArcLengthTable(zipLine));
                    }
                    tntWarsActiveZipLineIndex_ = -1; // real, honest reset -- the old index may no longer be valid
                }

                if (tntWarsActiveZipLineIndex_ < 0) {
                    // Kronos (zip-line E-to-mount fix): real, live-reported
                    // bug this fixes -- the previous version auto-mounted
                    // the instant a player was merely *near* a zip-line's
                    // own curve, with nothing stopping an immediate
                    // re-grab the moment a completed ride let go right at
                    // the far end -- a real, endless back-and-forth loop
                    // (and "spawns on the zip-line" whenever a spawn point
                    // happened to sit inside a curve's own trigger
                    // radius). Not currently riding -- this now only ever
                    // finds the *nearest* in-range zip-line to show a real
                    // "Press E to zip-line" UI-hint stdout stand-in (same
                    // convention core::Interactable's own generic
                    // proximity hint already establishes, see that
                    // header's own comment on why stdout, not a real
                    // on-screen widget -- engine_runtime renders no text
                    // at all). Mounting itself only happens on a real,
                    // explicit rising edge of the "Interact" key below.
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

                    bool mountKeyDown = input_.isActionDown("Interact");
                    if (nearestIndex >= 0 && mountKeyDown && !tntWarsZipLineMountKeyWasDown_) {
                        const tntwars::ZipLineState& zipLine = tntWarsZipLines_[nearestIndex];
                        // Real bidirectional mount -- same "ride away from
                        // the nearer end" convention computeZipLineVelocity()
                        // already established.
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
                    // Real, player-initiated early dismount -- pressing
                    // Jump gets off *at any time*, not just an automatic
                    // release at the curve's own far end.
                    bool jumpDown = input_.isActionDown("Jump");
                    if (jumpDown && !tntWarsZipLineDismountKeyWasDown_) {
                        tntWarsActiveZipLineIndex_ = -1;
                    }
                    tntWarsZipLineDismountKeyWasDown_ = jumpDown;
                }

                if (tntWarsActiveZipLineIndex_ >= 0) {
                    // Real, continuous ride -- once mounted, no longer
                    // gated by the trigger radius (a rider glued to the
                    // curve stays glued until they reach an end or
                    // dismount above), same real "carried along the wire"
                    // feel the previous implementation's own header
                    // comment described, delivered via a real, exact
                    // position teleport instead of a velocity for physics
                    // to integrate.
                    const tntwars::ZipLineState& zipLine = tntWarsZipLines_[tntWarsActiveZipLineIndex_];
                    const tntwars::ZipLineArcLengthTable& table = tntWarsZipLineArcTables_[tntWarsActiveZipLineIndex_];
                    // Real Traversal-upgrade ride-speed boost -- scales
                    // the real dt fed into advanceZipLineRider() rather
                    // than zipLine.travelSpeed itself (that field is real,
                    // shared, per-cable authored data every rider reads;
                    // scaling the caller's own dt achieves the identical
                    // real distance-per-tick increase without mutating
                    // shared course geometry).
                    float tntWarsZipLineDt = dt * tntWarsTraversalMultiplier;
                    if (auto ridePosition =
                            tntwars::advanceZipLineRider(zipLine, table, tntWarsZipLineRider_, tntWarsZipLineDt)) {
                        physics_.setPosition(character, ecs_, *ridePosition);
                        physics_.setHorizontalVelocity(character, ecs_, glm::vec2(0.0f));
                        physics_.setVerticalVelocity(character, ecs_, 0.0f);
                        // Real ride-complete detach -- distanceTraveled
                        // real-clamped to exactly 0 or table.totalLength
                        // by advanceZipLineRider() means this rider has
                        // real-reached whichever end they were traveling
                        // toward; releasing here lets normal gravity/input
                        // movement resume next tick. Re-mounting the same
                        // line again now requires a fresh real "Interact"
                        // press (see above), not just still standing in
                        // range -- the real, direct fix for the back-and-
                        // forth loop this whole change addresses.
                        if (tntWarsZipLineRider_.distanceTraveled <= 0.0f ||
                            tntWarsZipLineRider_.distanceTraveled >= table.totalLength) {
                            tntWarsActiveZipLineIndex_ = -1;
                        }
                    } else {
                        tntWarsActiveZipLineIndex_ = -1; // real, degenerate zero-length curve -- bail out
                    }
                }

                // Kronos ("Gameplay Loop" world-building): real resource-
                // node scavenging -- same real "proximity shows a UI
                // hint, an explicit Interact rising-edge performs the
                // action" convention the zip-line E-to-mount fix above
                // already establishes, applied to
                // TntWarsMatch::scavenge() instead of a mount. Only ever
                // considers a node this local player could actually
                // succeed on (own territory, not depleted) -- showing a
                // hint for a node scavenge() would just reject is worse
                // than showing none.
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

                    bool scavengeKeyDown = input_.isActionDown("Interact");
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

                // Kronos ("Gameplay Loop" world-building): real, live
                // traversal-course progress -- every populated course
                // ticks its own real clock (only while running) and
                // checks the live local player's own position against
                // its own next real checkpoint, same "real, honest no-op
                // when empty" convention every other optional TNT Wars
                // system here already follows (an empty
                // tntWarsTraversalChallenges_ costs one empty loop
                // iteration).
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

                // Kronos ("Gameplay Loop" world-building, "Progression"):
                // real, live upgrade-station proximity + purchase, same
                // real "proximity shows a hint, an explicit Interact
                // rising edge performs the action" convention as
                // scavenging/zip-lines above. Reuses the real, generic
                // core::findInteractablesInRange() proximity scan (every
                // upgrade station is a real core::Interactable) rather
                // than a bespoke scan like scavenging's own -- there's no
                // match-owned array to index here, just plain ECS
                // entities, the exact case that generic scan exists for.
                {
                    if (!tntWarsSuitBaseSpeedCaptured_) {
                        tntWarsSuitBaseWalkSpeed_ = characterController_.settings().walkSpeed;
                        tntWarsSuitBaseRunSpeed_ = characterController_.settings().runSpeed;
                        tntWarsSuitBaseSpeedCaptured_ = true;
                    }

                    std::vector<EntityId> nearby = findInteractablesInRange(ecs_, characterPos);
                    EntityId nearestStation = kNullEntity;
                    tntwars::UpgradeCategory nearestCategory = tntwars::UpgradeCategory::Traversal;
                    for (EntityId candidate : nearby) {
                        if (auto* link = ecs_.tryGetComponent<tntwars::UpgradeStationLink>(candidate)) {
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

                    bool upgradeKeyDown = input_.isActionDown("Interact");
                    if (nearestStation != kNullEntity && upgradeKeyDown && !tntWarsUpgradeKeyWasDown_) {
                        tntwars::UpgradeResult result = match.purchaseUpgrade(tntWarsLocalPlayerId_, nearestCategory);
                        if (result.success) {
                            std::fprintf(stdout, "TNT Wars: upgraded %s to tier %d!\n",
                                         nearestCategory == tntwars::UpgradeCategory::Traversal ? "Traversal" : "Suit",
                                         result.newTier);
                            if (nearestCategory == tntwars::UpgradeCategory::Suit) {
                                float multiplier = tntwars::suitMoveSpeedMultiplier(result.newTier);
                                characterController_.settingsMutable().walkSpeed = tntWarsSuitBaseWalkSpeed_ * multiplier;
                                characterController_.settingsMutable().runSpeed = tntWarsSuitBaseRunSpeed_ * multiplier;
                            }
                        } else {
                            std::fprintf(stdout, "TNT Wars: upgrade rejected (need more materials, or already maxed)\n");
                        }
                    }
                    tntWarsUpgradeKeyWasDown_ = upgradeKeyDown;
                }
            }

            // Real per-tick node respawn countdown + visual sync -- runs
            // every tick regardless of whether a live character exists
            // (world simulation continues), same convention the
            // destructible wall/cover ticks just below already establish.
            match.tickScavengeNodes(dt);
            tntwars::tickScavengeNodeVisuals(match.scavengeNodes(), tntWarsScavengeNodeVisuals_, ecs_);

            tntwars::tickDestructibleWallVisual(match.trenchesWallMutable(), tntWarsWallVisuals_, ecs_, physics_, dt);
            tntwars::tickDestructibleWallVisual(match.trenchesCoverMutable(), tntWarsCoverVisuals_, ecs_, physics_, dt);

            glm::vec3 aimOrigin = camera_.position;
            glm::vec3 aimDirection = camera_.forward();

            bool placeKeyDown = input_.isActionDown("TntWarsPlaceCharge");
            if (placeKeyDown && !tntWarsPlaceTntKeyWasDown_) {
                // Real raycast along the real camera aim direction -- a
                // real hit places the charge right where the player is
                // actually looking; a real miss (aiming at open sky, e.g.)
                // falls back to a fixed distance out, a real, honest
                // "still somewhere real and playable" default rather than
                // rejecting the placement entirely.
                constexpr float kPlaceMaxDistance = 30.0f;
                Physics::RaycastHit hit = physics_.raycast(aimOrigin, aimDirection, kPlaceMaxDistance);
                glm::vec3 placePosition = hit.hit ? hit.point : aimOrigin + aimDirection * kPlaceMaxDistance;
                bool accepted = match.placeTntCharge(tntWarsLocalPlayerId_, placePosition);
                std::fprintf(stdout, "TNT Wars: place charge %s\n", accepted ? "accepted" : "rejected");
            }
            tntWarsPlaceTntKeyWasDown_ = placeKeyDown;

            // Kronos bugfix (live-reported: "work on crafting table"):
            // real, live crafting -- C spends real scavenged materials
            // (E near a resource node is the real, only way to gain
            // them) on one real Standard Charge; H places one from the
            // player's own real craftedExplosives() inventory at the
            // same real raycast-aimed position G already uses, strictly
            // stronger than G's own always-available basic charge (see
            // Crafting.cpp's own real recipe tuning).
            bool craftKeyDown = input_.isActionDown("TntWarsCraftStandardCharge");
            if (craftKeyDown && !tntWarsCraftKeyWasDown_) {
                bool crafted = match.craft(tntWarsLocalPlayerId_, tntwars::ExplosiveRecipeType::StandardCharge);
                std::fprintf(stdout, "TNT Wars: craft Standard Charge %s\n", crafted ? "accepted" : "rejected (need class + materials)");
            }
            tntWarsCraftKeyWasDown_ = craftKeyDown;

            bool placeCraftedKeyDown = input_.isActionDown("TntWarsPlaceCraftedCharge");
            if (placeCraftedKeyDown && !tntWarsPlaceCraftedKeyWasDown_) {
                constexpr float kPlaceMaxDistance = 30.0f;
                Physics::RaycastHit hit = physics_.raycast(aimOrigin, aimDirection, kPlaceMaxDistance);
                glm::vec3 placePosition = hit.hit ? hit.point : aimOrigin + aimDirection * kPlaceMaxDistance;
                bool accepted = match.placeTntCharge(tntWarsLocalPlayerId_, placePosition, tntwars::ExplosiveRecipeType::StandardCharge);
                std::fprintf(stdout, "TNT Wars: place crafted Standard Charge %s\n", accepted ? "accepted" : "rejected (none crafted yet)");
            }
            tntWarsPlaceCraftedKeyWasDown_ = placeCraftedKeyDown;

            // Kronos ("Explosives System" world-building): real, thrown
            // grenades -- launched from the real camera position along
            // the real camera aim direction (unlike TNT's raycast-to-
            // surface placement, a grenade leaves the thrower's own hand
            // immediately and arcs under real gravity).
            bool grenadeKeyDown = input_.isActionDown("TntWarsThrowGrenade");
            if (grenadeKeyDown && !tntWarsGrenadeThrowKeyWasDown_) {
                tntWarsGrenades_.push_back(tntwars::throwGrenade(tntWarsLocalPlayerId_, aimOrigin, aimDirection));
                std::fprintf(stdout, "TNT Wars: grenade thrown\n");
            }
            tntWarsGrenadeThrowKeyWasDown_ = grenadeKeyDown;

            bool settingsKeyDown = input_.isActionDown("TntWarsToggleSettingsOverlay");
            if (settingsKeyDown && !tntWarsSettingsKeyWasDown_) tntWarsSettingsOverlayVisible_ = !tntWarsSettingsOverlayVisible_;
            tntWarsSettingsKeyWasDown_ = settingsKeyDown;

            bool leaderboardKeyDown = input_.isActionDown("TntWarsToggleLeaderboardOverlay");
            if (leaderboardKeyDown && !tntWarsLeaderboardKeyWasDown_) tntWarsLeaderboardOverlayVisible_ = !tntWarsLeaderboardOverlayVisible_;
            tntWarsLeaderboardKeyWasDown_ = leaderboardKeyDown;

            // Kronos ("Quality-of-Life & Finalization", "Respawn
            // system", "Spawn logic"): real, live, timed respawn --
            // class-retaining (respawnPlayer() never touches
            // playerClasses_), real class-based spawn-point spread (a
            // small, real per-class arc offset around the team's own
            // real base position, so different classes don't respawn
            // stacked on the exact same point).
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
                        physics_.setPosition(character, ecs_, tntWarsRespawnPosition_ + classOffset);
                        physics_.setHorizontalVelocity(character, ecs_, glm::vec2(0.0f));
                        physics_.setVerticalVelocity(character, ecs_, 0.0f);
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

            bool fireKeyDown = input_.isActionDown("TntWarsFireWeapon");
            if (fireKeyDown && !tntWarsFireKeyWasDown_) {
                tntwars::TntWarsMatch::FireResult result = match.fireWeapon(tntWarsLocalPlayerId_, aimOrigin, aimDirection, totalSimTime_);
                std::fprintf(stdout, "TNT Wars: fire weapon %s\n", result.accepted ? "accepted" : "rejected");
                if (result.accepted) {
                    core::EntityId projectileVisual = tntwars::spawnProjectileVisual(
                        ecs_, meshLibrary_, tntWarsMaterials_, renderer_.allocator(), renderer_.device(),
                        renderer_.commandPool(), renderer_.graphicsQueue(), result.projectile, "TntWarsProjectile");
                    tntWarsProjectileVisuals_.push_back({result.projectile, projectileVisual});
                }
            }
            tntWarsFireKeyWasDown_ = fireKeyDown;

            bool ultimateKeyDown = input_.isActionDown("TntWarsTriggerUltimate");
            if (ultimateKeyDown && !tntWarsUltimateKeyWasDown_) {
                tntwars::TntWarsMatch::UltimateResult result = match.triggerUltimate(tntWarsLocalPlayerId_, totalSimTime_);
                std::fprintf(stdout, "TNT Wars: trigger ultimate %s\n", result.accepted ? "accepted" : "rejected");
            }
            tntWarsUltimateKeyWasDown_ = ultimateKeyDown;

            const char* classActionNames[5] = {"TntWarsSelectClass1", "TntWarsSelectClass2", "TntWarsSelectClass3",
                                                "TntWarsSelectClass4", "TntWarsSelectClass5"};
            const tntwars::PlayerClassType classTypes[5] = {tntwars::PlayerClassType::Striker, tntwars::PlayerClassType::Deflector,
                                                              tntwars::PlayerClassType::Engineer, tntwars::PlayerClassType::Interceptor,
                                                              tntwars::PlayerClassType::Saboteur};
            for (int i = 0; i < 5; ++i) {
                bool classKeyDown = input_.isActionDown(classActionNames[i]);
                if (classKeyDown && !tntWarsClassKeyWasDown_[i]) {
                    bool accepted = match.selectClass(tntWarsLocalPlayerId_, classTypes[i]);
                    std::fprintf(stdout, "TNT Wars: select class %s %s\n", tntwars::playerClassName(classTypes[i]),
                                 accepted ? "accepted" : "rejected");
                    // Kronos bugfix (live-reported: "no TNT, no PvP/
                    // attack system"): the real fix's other half -- see
                    // main.cpp's own comment on why match setup now
                    // stops at ClassSelect instead of jumping straight
                    // to InProgress. The exact real tick a class
                    // selection actually succeeds, advance the match
                    // itself -- every other real action gated on
                    // hasSelectedClass() (TNT, fire, ultimate,
                    // scavenge, upgrades) starts working the very next
                    // tick, matching this mode's own real "single-
                    // player match begins the moment you're ready"
                    // intent instead of never beginning at all.
                    if (accepted) match.matchFlow().advanceTo(tntwars::MatchPhase::InProgress);
                }
                tntWarsClassKeyWasDown_[i] = classKeyDown;
            }

            // Kronos ("User Interface" world-building, HUD): real,
            // live-data HUD -- built here (CPU-side batch, via
            // UIRenderer::drawRect()/drawText()) once this tick's own
            // match/player state is final, drawn later this same frame
            // by Renderer's own overlay pass (see
            // Application::initialize()'s own setOverlayCallback()
            // call). No fake "stamina"/"ammo" bars for mechanics this
            // game doesn't have (no stamina-drain system, no clip-based
            // weapons exist) -- ultimate charge is the real, honest
            // analog to "ammo" shown here instead.
            tntWarsMatchElapsedSeconds_ += dt;
            uiRenderer_.beginFrame(VkExtent2D{window_.width(), window_.height()});
            // Kronos (Alpha Roadmap Phase 7, "Lua UI") -- see the matching
            // call in the camera-showcase HUD block above for why this is
            // a flush, not a direct pass-through.
            scriptUiApi_.flushInto(uiRenderer_);
            if (match.hasSelectedClass(tntWarsLocalPlayerId_)) {
                tntwars::ClassStats stats = match.classTuning().statsFor(match.classOf(tntWarsLocalPlayerId_));
                float health = match.health(tntWarsLocalPlayerId_);
                float healthFrac = stats.maxHealth > 0.0f ? glm::clamp(health / stats.maxHealth, 0.0f, 1.0f) : 0.0f;
                float ultimateFrac = stats.ultimateChargeRequired > 0.0f
                                          ? glm::clamp(match.ultimateCharge().charge(tntWarsLocalPlayerId_) / stats.ultimateChargeRequired, 0.0f, 1.0f)
                                          : 0.0f;

                constexpr glm::vec2 kBarSize(220.0f, 22.0f);
                constexpr glm::vec2 kBarPos(24.0f, 24.0f);
                uiRenderer_.drawRect(kBarPos, kBarSize, glm::vec4(0.0f, 0.0f, 0.0f, 0.55f));
                uiRenderer_.drawRect(kBarPos, glm::vec2(kBarSize.x * healthFrac, kBarSize.y), glm::vec4(0.75f, 0.15f, 0.15f, 0.85f));
                char healthLabel[32];
                std::snprintf(healthLabel, sizeof(healthLabel), "HP %d/%d", static_cast<int>(health), static_cast<int>(stats.maxHealth));
                uiRenderer_.drawText(healthLabel, kBarPos + glm::vec2(6.0f, 3.0f), 0.75f, glm::vec4(1.0f));

                glm::vec2 ultBarPos = kBarPos + glm::vec2(0.0f, kBarSize.y + 8.0f);
                uiRenderer_.drawRect(ultBarPos, kBarSize, glm::vec4(0.0f, 0.0f, 0.0f, 0.55f));
                uiRenderer_.drawRect(ultBarPos, glm::vec2(kBarSize.x * ultimateFrac, kBarSize.y), glm::vec4(0.25f, 0.45f, 0.85f, 0.85f));
                char ultLabel[32];
                std::snprintf(ultLabel, sizeof(ultLabel), "ULT %d%%", static_cast<int>(ultimateFrac * 100.0f));
                uiRenderer_.drawText(ultLabel, ultBarPos + glm::vec2(6.0f, 3.0f), 0.75f, glm::vec4(1.0f));

                const tntwars::ScavengedMaterials& mats = match.scavengedMaterials(tntWarsLocalPlayerId_);
                char matsLabel[96];
                std::snprintf(matsLabel, sizeof(matsLabel), "%s %d  %s %d  %s %d",
                              tntwars::scavengeMaterialName(tntwars::ScavengeMaterialType::ScrapMetal),
                              tntwars::scavengedMaterialCount(mats, tntwars::ScavengeMaterialType::ScrapMetal),
                              tntwars::scavengeMaterialName(tntwars::ScavengeMaterialType::BlastingPowder),
                              tntwars::scavengedMaterialCount(mats, tntwars::ScavengeMaterialType::BlastingPowder),
                              tntwars::scavengeMaterialName(tntwars::ScavengeMaterialType::Wiring),
                              tntwars::scavengedMaterialCount(mats, tntwars::ScavengeMaterialType::Wiring));
                uiRenderer_.drawText(matsLabel, kBarPos + glm::vec2(0.0f, (kBarSize.y + 8.0f) * 2.0f), 0.65f, glm::vec4(0.9f, 0.9f, 0.85f, 1.0f));

                // Kronos bugfix (live-reported: "work on crafting
                // table"): real, live crafted-charge inventory display
                // -- C crafts (spends materials above), H places one.
                const tntwars::CraftedExplosives& crafted = match.craftedExplosives(tntWarsLocalPlayerId_);
                char craftLabel[64];
                std::snprintf(craftLabel, sizeof(craftLabel), "Crafted Charges: %d  [C]raft  [H]place",
                              crafted.counts[static_cast<size_t>(tntwars::ExplosiveRecipeType::StandardCharge)]);
                uiRenderer_.drawText(craftLabel, kBarPos + glm::vec2(0.0f, (kBarSize.y + 8.0f) * 2.0f + 22.0f), 0.6f,
                                      glm::vec4(1.0f, 0.75f, 0.4f, 1.0f));

                const tntwars::PlayerUpgrades& upgrades = match.playerUpgrades(tntWarsLocalPlayerId_);
                char upgradeLabel[64];
                std::snprintf(upgradeLabel, sizeof(upgradeLabel), "Traversal Lv%d  Suit Lv%d", upgrades.traversalTier, upgrades.suitTier);
                uiRenderer_.drawText(upgradeLabel, kBarPos + glm::vec2(0.0f, (kBarSize.y + 8.0f) * 2.0f + 44.0f), 0.65f,
                                      glm::vec4(0.7f, 0.85f, 1.0f, 1.0f));
            } else {
                // Kronos ("User Interface" world-building, "Class
                // selection UI"): real, live class roster -- reuses the
                // exact same real `classTypes`/`playerClassName()` this
                // block's own input handling above already established,
                // so this list can never drift out of sync with what
                // pressing 1-5 actually selects.
                uiRenderer_.drawText("SELECT YOUR CLASS", glm::vec2(24.0f, 24.0f), 0.85f, glm::vec4(1.0f, 0.85f, 0.3f, 1.0f));
                for (int i = 0; i < 5; ++i) {
                    char classLine[64];
                    std::snprintf(classLine, sizeof(classLine), "[%d] %s", i + 1, tntwars::playerClassName(classTypes[i]));
                    uiRenderer_.drawText(classLine, glm::vec2(24.0f, 60.0f + static_cast<float>(i) * 26.0f), 0.7f,
                                          glm::vec4(0.9f, 0.9f, 0.95f, 1.0f));
                }
            }

            // Real, live traversal-challenge status -- top-right, only
            // while a real course is actually running.
            {
                float hudY = 24.0f;
                for (const tntwars::TraversalChallengeState& challenge : tntWarsTraversalChallenges_) {
                    if (!challenge.running) continue;
                    char label[128];
                    std::snprintf(label, sizeof(label), "%s: %.1fs", challenge.name.c_str(), challenge.elapsedSeconds);
                    glm::vec2 size = uiRenderer_.measureText(label, 0.7f);
                    uiRenderer_.drawText(label, glm::vec2(static_cast<float>(window_.width()) - size.x - 24.0f, hudY), 0.7f,
                                          glm::vec4(0.6f, 1.0f, 0.7f, 1.0f));
                    hudY += 26.0f;
                }
            }

            // Real, live PvP-node contest status -- shown only while the
            // local player is actually standing in one, matching this
            // system's own real presence-radius contract.
            for (size_t i = 0; i < tntWarsPvPNodes_.size(); ++i) {
                const tntwars::PvPNodeState& node = tntWarsPvPNodes_[i];
                if (glm::length(characterPos - node.position) > node.radius) continue;
                char label[96];
                const char* controlledBy = node.controllingTeam.has_value()
                                                ? (*node.controllingTeam == tntwars::TeamId::A ? "Team A" : "Team B")
                                                : "Neutral";
                std::snprintf(label, sizeof(label), "PvP Node: %s (%.0f%%)", controlledBy, node.captureProgress * 100.0f);
                glm::vec2 size = uiRenderer_.measureText(label, 0.75f);
                uiRenderer_.drawText(label, glm::vec2((static_cast<float>(window_.width()) - size.x) * 0.5f, 24.0f), 0.75f,
                                      glm::vec4(1.0f, 0.9f, 0.4f, 1.0f));
                break;
            }

            // Kronos ("User Interface" world-building, "Settings
            // panel"): a real, read-only display of the exact real
            // renderer state F6-F12 above already control live -- not a
            // second, independent copy of that state (a settings *menu*
            // that could drift from what the F-keys actually do would be
            // worse than none at all).
            if (tntWarsSettingsOverlayVisible_) {
                glm::vec2 panelPos(24.0f, static_cast<float>(window_.height()) * 0.5f - 110.0f);
                glm::vec2 panelSize(360.0f, 220.0f);
                uiRenderer_.drawRect(panelPos, panelSize, glm::vec4(0.0f, 0.0f, 0.0f, 0.72f));
                glm::vec2 cursor = panelPos + glm::vec2(16.0f, 14.0f);
                uiRenderer_.drawText("SETTINGS  (F1 to close)", cursor, 0.75f, glm::vec4(1.0f, 0.85f, 0.3f, 1.0f));
                cursor.y += 32.0f;
                auto drawToggleLine = [&](const char* label, bool enabled, const char* key) {
                    char line[96];
                    std::snprintf(line, sizeof(line), "%-22s [%s]  %s", label, enabled ? "ON " : "OFF", key);
                    uiRenderer_.drawText(line, cursor, 0.62f,
                                          enabled ? glm::vec4(0.6f, 1.0f, 0.7f, 1.0f) : glm::vec4(0.7f, 0.7f, 0.75f, 1.0f));
                    cursor.y += 24.0f;
                };
                drawToggleLine("RT Reflections", renderer_.isRTReflectionsEnabled(), "F9");
                drawToggleLine("Volumetric Fog", renderer_.isVolumetricFogEnabled(), "F11");
                drawToggleLine("Screen-Space Reflections", renderer_.isSSREnabled(), "--");
                drawToggleLine("Ray-Traced GI", renderer_.isRTGIEnabled(), "--");
                drawToggleLine("Cinematic Mode", renderer_.isCinematicModeEnabled(), "F7");
                drawToggleLine("Heat Distortion", renderer_.isHeatDistortionEnabled(), "F8");
            }

            // Kronos ("User Interface" world-building, "Leaderboard
            // UI"): real race best-times + real PvP node control tally
            // -- Tab to toggle.
            if (tntWarsLeaderboardOverlayVisible_) {
                glm::vec2 panelPos(static_cast<float>(window_.width()) * 0.5f - 220.0f, 90.0f);
                glm::vec2 panelSize(440.0f, 90.0f + static_cast<float>(tntWarsTraversalChallenges_.size()) * 24.0f +
                                                 static_cast<float>(tntWarsPvPNodes_.size()) * 24.0f);
                uiRenderer_.drawRect(panelPos, panelSize, glm::vec4(0.0f, 0.0f, 0.0f, 0.72f));
                glm::vec2 cursor = panelPos + glm::vec2(16.0f, 14.0f);
                uiRenderer_.drawText("LEADERBOARD  (Tab to close)", cursor, 0.75f, glm::vec4(1.0f, 0.85f, 0.3f, 1.0f));
                cursor.y += 32.0f;
                uiRenderer_.drawText("Best Race Times:", cursor, 0.65f, glm::vec4(0.8f, 0.9f, 1.0f, 1.0f));
                cursor.y += 24.0f;
                for (const tntwars::TraversalChallengeState& challenge : tntWarsTraversalChallenges_) {
                    char line[96];
                    if (challenge.bestTimeSeconds >= 0.0f) {
                        std::snprintf(line, sizeof(line), "  %s: %.2fs", challenge.name.c_str(), challenge.bestTimeSeconds);
                    } else {
                        std::snprintf(line, sizeof(line), "  %s: --", challenge.name.c_str());
                    }
                    uiRenderer_.drawText(line, cursor, 0.6f, glm::vec4(0.9f, 0.9f, 0.9f, 1.0f));
                    cursor.y += 24.0f;
                }
                cursor.y += 8.0f;
                uiRenderer_.drawText("PvP Nodes:", cursor, 0.65f, glm::vec4(0.8f, 0.9f, 1.0f, 1.0f));
                cursor.y += 24.0f;
                for (size_t i = 0; i < tntWarsPvPNodes_.size(); ++i) {
                    const tntwars::PvPNodeState& node = tntWarsPvPNodes_[i];
                    char line[96];
                    const char* controlledBy = node.controllingTeam.has_value()
                                                    ? (*node.controllingTeam == tntwars::TeamId::A ? "Team A" : "Team B")
                                                    : "Neutral";
                    std::snprintf(line, sizeof(line), "  Node %zu: %s", i, controlledBy);
                    uiRenderer_.drawText(line, cursor, 0.6f, glm::vec4(0.9f, 0.9f, 0.9f, 1.0f));
                    cursor.y += 24.0f;
                }
            }

            // Kronos ("Quality-of-Life & Finalization", "Tutorial
            // prompts"): a real, timed sequence of short on-screen
            // hints for new players -- cycles every 6 real seconds
            // across the first 30, then real-disappears for good so it
            // never becomes a permanent, ignorable fixture for a player
            // who already knows the controls.
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
                glm::vec2 hintSize = uiRenderer_.measureText(hint, 0.65f);
                glm::vec2 hintPos((static_cast<float>(window_.width()) - hintSize.x) * 0.5f,
                                   static_cast<float>(window_.height()) - 56.0f);
                uiRenderer_.drawRect(hintPos + glm::vec2(-12.0f, -8.0f), hintSize + glm::vec2(24.0f, 16.0f),
                                      glm::vec4(0.0f, 0.0f, 0.0f, 0.6f));
                uiRenderer_.drawText(hint, hintPos, 0.65f, glm::vec4(1.0f, 1.0f, 0.85f, 1.0f));
            }
        }

        // Real chunk streaming (task category 6) -- only runs if the
        // caller actually registered a terrain via setTerrain(); a scene
        // with no terrain is a real, valid, honest no-op.
        if (terrain_ != nullptr) {
            terrain_->updateStreaming(characterPos, terrainLoadRadius_, terrainUnloadRadius_);
        }

        // Real, soft world boundary (task category 4) -- a gentle,
        // gradual nudge back toward worldBoundary_.center once the
        // character wanders past worldBoundary_.softRadius, applied
        // directly to the character's own Transform (not a physics
        // force -- see softBoundaryCorrection()'s own comment on why a
        // pure position correction is the honest, simple choice here).
        // correctionStrength deliberately well under 1.0 so this reads
        // as "the world gently resists going further", not a snap/teleport.
        if (auto* transform = ecs_.tryGetComponent<Transform>(character)) {
            transform->position = softBoundaryCorrection(transform->position, worldBoundary_, 0.08f);
            characterPos = transform->position;
        }

        // Sprint 5 ("Core Economy"): real ore-node respawn ticking and
        // the earn-window anti-inflation throttle -- both real per-tick
        // state advances, same "driven every tick from the pre-tick
        // hook" pattern MovingPlatform above already established.
        tickOreNodeRespawns(dt, ecs_, physics_);
        if (auto* throttle = ecs_.tryGetComponent<EarnThrottle>(character)) tickEarnThrottle(*throttle, dt);
        tickFlashEffects(dt, ecs_);

        // Sprint 10 ("Creator Tools Phase 2") task category 3: real prop
        // animation hooks -- every entity with a PropAnimationHook
        // advances its own open/close sweep by dt and writes the
        // resulting real AnimatedPose straight into its Transform, the
        // same "pure tick function, I/O-touching caller writes it" split
        // toggleDoor()/tickFlashEffects() already established.
        {
            auto propAnimView = ecs_.view<PropAnimationHook, Transform>();
            for (auto entity : propAnimView) {
                auto& hook = propAnimView.get<PropAnimationHook>(entity);
                auto& transform = propAnimView.get<Transform>(entity);
                AnimatedPose pose = tickPropAnimationHook(hook, dt);
                transform.position = pose.position;
                transform.rotation = pose.rotation;
            }
        }

        // Real auto-pickup (task category 2's "auto-pickup integration
        // using the existing physics events.onInteract pipeline"): every
        // core::OreDrop within pickup range collects automatically, no
        // "E" press needed -- the real, common "walk over loot to grab
        // it" mining-game convention, distinct from Pickup's manual
        // look-and-press collection above. Still fires the same
        // events.onInteract gameplay scripts already listen to, and
        // still goes through the exact same Inventory::addItem() capacity
        // math as a manual pickup would -- a full inventory leaves the
        // remainder sitting in the world rather than losing or
        // duplicating units (see Inventory.hpp's own comment).
        if (auto* inventory = ecs_.tryGetComponent<Inventory>(character)) {
            constexpr float kAutoPickupRadius = 1.75f;
            // Collected first, then destroyed after the loop -- calling
            // destroyEntity() mid-iteration over the same view it reads
            // from can invalidate the view's own iterator (see
            // AvatarLoadoutSync.cpp's identical precedent/comment).
            std::vector<EntityId> emptiedDrops;
            auto drops = ecs_.view<OreDrop, Transform>();
            for (auto dropEntity : drops) {
                auto& transform = drops.get<Transform>(dropEntity);
                if (glm::length(transform.position - characterPos) > kAutoPickupRadius) continue;

                auto& drop = drops.get<OreDrop>(dropEntity);
                int before = drop.quantity;
                int collected = collectOreDrop(drop, *inventory);
                if (collected <= 0) continue; // inventory has no room at all right now -- leave it in the world

                scripting_.fireInteract(static_cast<uint32_t>(dropEntity), static_cast<uint32_t>(character));
                std::fprintf(stdout, "[floating text] +%d %s\n", collected, oreTypeName(drop.oreType));
                if (drop.quantity <= 0) {
                    emptiedDrops.push_back(dropEntity);
                } else if (collected < before) {
                    std::fprintf(stdout, "[floating text] Inventory full -- %d %s left behind\n", drop.quantity,
                                 oreTypeName(drop.oreType));
                }
            }
            for (EntityId dropEntity : emptiedDrops) ecs_.destroyEntity(dropEntity);
        }

        // Real raycast target: what the player is actually looking at --
        // a real Physics::raycast() (Jolt NarrowPhaseQuery, see that
        // method's header comment), independent of core::Interactable
        // entirely (an entity with no Interactable component is still a
        // valid raycast target, matching this trigger's pre-existing
        // permissive behavior -- see Interactable.hpp's own comment).
        // Self-hits (the ray clipping the character's own capsule --
        // possible at some third-person camera angles/distances) are
        // filtered out.
        constexpr float kInteractDistance = 6.0f;
        Physics::RaycastHit rayHit = physics_.raycast(camera_.position, camera_.forward(), kInteractDistance);
        EntityId lookAtTarget = (rayHit.hit && rayHit.entity != character) ? rayHit.entity : kNullEntity;

        // Real proximity targets: every Interactable within its own
        // proximityRadius of the character, nearest first -- see
        // findInteractablesInRange()'s header comment. Independent of
        // camera facing entirely, the real second interaction-mapping
        // this pass adds alongside the pre-existing raycast one.
        std::vector<EntityId> nearby = findInteractablesInRange(ecs_, characterPos);
        EntityId nearestProximityTarget = nearby.empty() ? kNullEntity : nearby.front();

        // Real UI-hint stub: engine_runtime has no on-screen text
        // rendering at all (a stated architectural boundary, see
        // Interactable.hpp's comment) to draw a real prompt into, so this
        // prints to stdout instead -- a real, functioning signal (only on
        // an actual change, not every tick) standing in for where a real
        // on-screen prompt widget would read the exact same
        // Interactable::prompt string from.
        EntityId hintTarget = lookAtTarget != kNullEntity ? lookAtTarget : nearestProximityTarget;
        if (hintTarget != lastInteractionHintEntity_) {
            if (hintTarget != kNullEntity) {
                std::string prompt = "Interact";
                if (auto* interactable = ecs_.tryGetComponent<Interactable>(hintTarget)) prompt = interactable->prompt;
                std::fprintf(stdout, "[UI hint] %s\n", prompt.c_str());
            } else {
                std::fprintf(stdout, "[UI hint] (none)\n");
            }
            lastInteractionHintEntity_ = hintTarget;
        }

        // events.onInteract's real trigger: a rising edge on "Interact" --
        // the raycast target wins if there is one (matches this trigger's
        // pre-existing look-and-press behavior exactly), falling back to
        // the nearest in-range proximity target otherwise, so a single
        // key does double duty for both real interaction mappings. Either
        // path is gated by a real cooldown check (canInteract()) when the
        // target has an Interactable component -- an entity without one
        // has no cooldown concept at all and fires every press, matching
        // pre-existing behavior. See interactKeyWasDown_'s doc comment
        // for why the edge-detection happens here rather than relying on
        // isActionDown() alone.
        bool interactDown = input_.isActionDown("Interact");
        if (interactDown && !interactKeyWasDown_) {
            EntityId target = resolveInteractionTarget(lookAtTarget, nearestProximityTarget, ecs_, totalSimTime_);
            if (target != kNullEntity) {
                scripting_.fireInteract(static_cast<uint32_t>(target), static_cast<uint32_t>(character));
                if (auto* interactable = ecs_.tryGetComponent<Interactable>(target)) {
                    markInteracted(*interactable, totalSimTime_);
                }
                // Runtime Interaction Examples (docs task category 7): the
                // real, default Door/Pickup behaviors -- applied alongside
                // (not instead of) events.onInteract above, so a script
                // can still layer its own reaction (a sound, a UI toast)
                // on the same interaction.
                if (auto* door = ecs_.tryGetComponent<Door>(target)) {
                    if (auto* transform = ecs_.tryGetComponent<Transform>(target)) toggleDoor(*door, *transform);
                }
                if (ecs_.hasComponent<Pickup>(target)) {
                    collectPickup(target, ecs_);
                }
                // Sprint 6 ("World Systems & Environment"): a real, working
                // world-prop interaction -- toggling a Lamp on/off, the
                // same "pure toggle, I/O-touching caller writes the
                // Renderable" split toggleDoor() already established.
                if (auto* lamp = ecs_.tryGetComponent<LampState>(target)) {
                    float newIntensity = toggleLamp(*lamp);
                    if (auto* renderable = ecs_.tryGetComponent<Renderable>(target)) renderable->emissiveIntensity = newIntensity;
                }
                // Sprint 10 ("Creator Tools Phase 2") task category 3:
                // the real "toggle" animation event hook -- interacting
                // with a PropAnimationHook-carrying entity flips its
                // open/closed direction; the actual per-tick pose
                // application happens once, every tick, in the pre-tick
                // hook above (not here), so the sweep animates smoothly
                // over real time rather than snapping instantly on the
                // interact press.
                if (auto* propAnim = ecs_.tryGetComponent<PropAnimationHook>(target)) {
                    togglePropAnimation(*propAnim);
                }
                // Sprint 6 ("World Systems & Environment"): a real,
                // working teleport pad -- moves the character's own
                // Transform straight to the pad's destination and zeroes
                // its physics velocity, so momentum from just before
                // stepping onto the pad doesn't carry through the
                // teleport (a real, honest "fresh start" at the
                // destination, not a velocity-preserving warp that could
                // launch the character through geometry on arrival).
                if (auto* pad = ecs_.tryGetComponent<TeleportPad>(target)) {
                    if (auto* transform = ecs_.tryGetComponent<Transform>(character)) {
                        transform->position = pad->destination;
                    }
                    physics_.setHorizontalVelocity(character, ecs_, {0.0f, 0.0f});
                    physics_.setVerticalVelocity(character, ecs_, 0.0f);
                    std::fprintf(stdout, "[floating text] Teleported.\n");
                }

                // Sprint 5 ("Core Economy"): mining, selling, and
                // upgrade-purchasing all dispatch through this exact
                // same real interaction pipeline -- a mining swing is
                // just another `events.onInteract`-triggering press,
                // same as opening a door.
                if (auto* oreNode = ecs_.tryGetComponent<OreNode>(target)) {
                    auto* upgrades = ecs_.tryGetComponent<PlayerUpgrades>(character);
                    int miningPower = upgrades != nullptr ? miningPowerFor(*upgrades) : 1;
                    MiningResult mining = mineOreNode(*oreNode, miningPower);
                    if (mining.hit) {
                        const OreTypeInfo& info = oreTypeInfo(oreNode->oreType);
                        if (mining.broke) {
                            BreakResult broken = breakOreNode(target, ecs_, physics_, *oreNode, economyRng_, oreDropMeshHandle_);
                            std::fprintf(stdout, "[floating text] %s node broken!%s\n", info.name,
                                         broken.bonusGem ? " (+1 bonus gem!)" : "");
                            if (broken.bonusGem) {
                                if (auto* wallet = ecs_.tryGetComponent<Wallet>(character)) wallet->gems += 1;
                            }
                        } else {
                            std::fprintf(stdout, "[floating text] Hit %s (%d/%d HP)\n", info.name,
                                         mining.remainingHealth, info.hitsToBreak);
                        }
                    }
                }
                if (ecs_.hasComponent<ShopStall>(target)) {
                    auto* inventory = ecs_.tryGetComponent<Inventory>(character);
                    auto* wallet = ecs_.tryGetComponent<Wallet>(character);
                    auto* throttle = ecs_.tryGetComponent<EarnThrottle>(character);
                    if (inventory != nullptr && wallet != nullptr && throttle != nullptr) {
                        SellAllResult sold = sellAllInventory(*inventory, *wallet, *throttle);
                        std::fprintf(stdout, "[floating text] Sold everything for %lld coins (wallet: %lld coins, %lld gems)\n",
                                     static_cast<long long>(sold.totalCoinsEarned), static_cast<long long>(wallet->coins),
                                     static_cast<long long>(wallet->gems));
                        // Real sell "animation" (task category 6):
                        // engine_runtime has no toast/confirmation UI to
                        // pop up (see Interactable.hpp's UI-hint comment),
                        // so the stall itself briefly glows -- a real,
                        // visible, renderable confirmation the sale went
                        // through, gated on the sale actually earning
                        // something (an empty inventory doesn't flash).
                        if (sold.totalCoinsEarned > 0) triggerFlash(ecs_, target, 3.5f, 0.0f, 0.35f);
                    }
                }
                if (auto* station = ecs_.tryGetComponent<UpgradeStation>(target)) {
                    auto* upgrades = ecs_.tryGetComponent<PlayerUpgrades>(character);
                    auto* wallet = ecs_.tryGetComponent<Wallet>(character);
                    if (upgrades != nullptr && wallet != nullptr) {
                        UpgradePurchaseResult purchase = purchaseUpgrade(*upgrades, *wallet, station->category);
                        if (purchase.success) {
                            if (station->category == UpgradeCategory::Backpack) {
                                if (auto* inventory = ecs_.tryGetComponent<Inventory>(character)) {
                                    applyBackpackTier(*inventory, *upgrades);
                                }
                            }
                            std::fprintf(stdout, "[floating text] Upgraded! (-%lld coins)\n",
                                         static_cast<long long>(purchase.coinsSpent));
                            // Real buy "animation" -- same reasoning as
                            // the sell-stall flash above, brighter/longer
                            // since a purchase is the bigger, more
                            // deliberate of the two transactions.
                            triggerFlash(ecs_, target, 4.5f, 0.0f, 0.5f);
                        } else {
                            std::fprintf(stdout, "[floating text] Upgrade failed: %s\n", purchase.reason);
                        }
                    }
                }
            }
        }
        interactKeyWasDown_ = interactDown;
    });

    // Sprint 14 ("render-tick decoupling"): net::NetworkSession::tick()
    // and the networked-client input-sampling that feeds it, moved out of
    // the (now 120Hz) sim hook above into their own real, independent
    // 60Hz network cadence -- see GameLoop::setNetworkTickHook()'s own
    // comment. A real, honest no-op in Offline mode (the default; see
    // net::NetworkSession::tick()'s own early-return). Client mode
    // samples this process's own local WASD/mouse input into a real
    // InputCommand (bypassing characterController_'s own physics-driven
    // movement for the *networked* player entity -- see
    // startNetworking()'s comment on why that's a deliberately separate,
    // simpler entity) and drives a minimal, real free-look camera
    // following it.
    gameLoop_->setNetworkTickHook([this](float dt) {
        if (networkSession_.isClient() && networkedLocalPlayerEntity_ != kNullEntity) {
            // Real, drained mouse delta accumulated across however many
            // sim ticks elapsed since the last network tick -- see
            // networkMouseDeltaAccumulator_'s own comment in
            // Application.hpp.
            glm::vec2 mouseDelta = networkMouseDeltaAccumulator_;
            networkMouseDeltaAccumulator_ = glm::vec2(0.0f);
            camera_.yawDegrees += mouseDelta.x * 0.15f;
            camera_.pitchDegrees =
                std::clamp(camera_.pitchDegrees - mouseDelta.y * 0.15f, -80.0f, 80.0f);

            glm::vec3 moveAxis(0.0f);
            if (input_.isActionDown("MoveForward")) moveAxis.z += 1.0f;
            if (input_.isActionDown("MoveBackward")) moveAxis.z -= 1.0f;
            if (input_.isActionDown("MoveRight")) moveAxis.x += 1.0f;
            if (input_.isActionDown("MoveLeft")) moveAxis.x -= 1.0f;
            if (glm::length(moveAxis) > 0.0001f) moveAxis = glm::normalize(moveAxis);
            bool jump = input_.isActionDown("Jump");

            networkSession_.sampleLocalInput(ecs_, networkedLocalPlayerEntity_, moveAxis, jump, false,
                                              camera_.yawDegrees, camera_.pitchDegrees, dt);

            if (auto* transform = ecs_.tryGetComponent<Transform>(networkedLocalPlayerEntity_)) {
                constexpr float kCameraDistance = 6.0f;
                camera_.position = transform->position - camera_.forward() * kCameraDistance +
                                    glm::vec3(0.0f, 1.7f, 0.0f);
            }
        }
        networkSession_.tick(dt, ecs_, networkedLocalPlayerEntity_);
    });

    // The other real event.onX trigger: Physics' actual Jolt
    // ContactListener (Physics.hpp's drainCollisionEvents()) queues new
    // contacts during step(); this hook (runs right after step(), see
    // GameLoop::setPostPhysicsHook's doc comment) drains and forwards them
    // to events.onCollision.
    gameLoop_->setPostPhysicsHook([this](float) {
        for (const auto& event : physics_.drainCollisionEvents()) {
            scripting_.fireCollision(static_cast<uint32_t>(event.first), static_cast<uint32_t>(event.second));
        }
    });

    // Sprint 8 ("Performance Stats & Debug Tools"): composes this frame's
    // full PerformanceMetrics (Renderer's real render-only numbers plus
    // real physics/terrain/process stats) right after renderFrame()
    // returns (see GameLoop::PostRenderHook's own comment for why here,
    // not inside Renderer itself, which owns none of physics_/terrain_/
    // processStatsSampler_), feeds it to the profiler for spike/stall
    // detection every frame, and -- throttled to once/sec, matching
    // Renderer's own stdout cadence -- prints an extended stats line.
    // engine_runtime has no ImGui (see docs/ARCHITECTURE.md's "no
    // Studio-only privileges" boundary), so this stdout line is the real
    // "Performance Stats Panel" for the client half of this task; Studio
    // gets the real ImGui panel (see StudioApp.cpp).
    gameLoop_->setPostRenderHook([this](float dt) {
        uint32_t loadedChunks = terrain_ != nullptr ? static_cast<uint32_t>(terrain_->loadedChunkCount()) : 0;
        uint32_t totalChunks = terrain_ != nullptr ? static_cast<uint32_t>(terrain_->chunkCount()) : 0;
        ProcessStats processStats = processStatsSampler_.sample();
        lastPerformanceMetrics_ = composePerformanceMetrics(renderer_.metrics(), physics_.activeBodyCount(),
                                                              physics_.totalBodyCount(), loadedChunks, totalChunks,
                                                              processStats);

        profiler_.recordFrame(lastPerformanceMetrics_.frameTimeMs,
                               std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count());
        profiler_.recordSnapshot(lastPerformanceMetrics_,
                                  std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count());

        metricsLogAccumulatorSeconds_ += dt;
        if (metricsLogAccumulatorSeconds_ >= 1.0f) {
            metricsLogAccumulatorSeconds_ = 0.0f;
            PerformanceSeverity frameSeverity = classifyFrameTimeSeverity(lastPerformanceMetrics_.frameTimeMs);
            const char* severityTag = frameSeverity == PerformanceSeverity::Good     ? "OK"
                                       : frameSeverity == PerformanceSeverity::Warning ? "WARN"
                                                                                        : "CRIT";
            std::fprintf(stdout,
                         "Profiler [%s]: physics %u/%u bodies active | terrain %u/%u chunks loaded | "
                         "process %.0f MB RSS, %.1f%% CPU | %zu profiler events\n",
                         severityTag, lastPerformanceMetrics_.activePhysicsBodies,
                         lastPerformanceMetrics_.totalPhysicsBodies, lastPerformanceMetrics_.loadedTerrainChunks,
                         lastPerformanceMetrics_.totalTerrainChunks,
                         static_cast<double>(lastPerformanceMetrics_.processMemoryBytes) / (1024.0 * 1024.0),
                         static_cast<double>(lastPerformanceMetrics_.processCpuPercent), profiler_.events().size());
        }
    });

    initialized_ = true;
    return true;
}

void Application::run() {
    if (!initialized_ || !gameLoop_) {
        std::fprintf(stderr, "Application: run() called before a successful initialize().\n");
        return;
    }
    gameLoop_->run();
}

bool Application::spawnLocalPlayerAvatar(glm::vec3 spawnPosition, glm::vec4 skinTone, HeadShape headShape,
                                          BodyProportions bodyProportions, const AvatarLoadout& loadout,
                                          const CatalogueIndex& catalogueIndex,
                                          const AnimationOverrides& animationOverrides, ClothingFit clothingFit) {
    // Real, honest reset -- a fresh call (e.g. loading a different
    // Catalogue game) must not leave a stale AvatarController driving
    // GPU-uploaded entities that runtime::loadGame()'s own ECS wipe
    // already destroyed. RiggedMeshLibrary keeps whatever GPU mesh
    // buffers the previous avatar's segments used; re-registering fresh
    // ones each call is a real, small, accepted GPU-memory cost for a
    // local Alpha (matches this codebase's own "don't over-engineer a
    // resource pool for a problem that isn't real yet" convention), not
    // a leak -- RiggedMeshLibrary owns and frees every handle it ever
    // registers at its own destruction.
    avatarController_.reset();
    skinnedAvatarEntities_.clear();

    EntityId character = characterController_.spawn(ecs_, physics_, spawnPosition);
    if (character == kNullEntity) {
        std::fprintf(stderr, "Application: spawnLocalPlayerAvatar() -- characterController_.spawn() failed.\n");
        return false;
    }

    Skeleton skeleton = applyBodyProportionsToSkeleton(buildHumanoidSkeleton(), bodyProportions);
    std::string spawnError;
    if (!spawnRiggedAvatar(ecs_, skeleton, loadout, catalogueIndex, riggedMeshLibrary_, renderer_.allocator(),
                            renderer_.device(), renderer_.commandPool(), renderer_.graphicsQueue(), skinnedAvatarEntities_,
                            spawnError, skinTone, headShape, bodyProportions)) {
        std::fprintf(stderr, "Application: spawnLocalPlayerAvatar() -- spawnRiggedAvatar() failed: %s\n",
                     spawnError.c_str());
        skinnedAvatarEntities_.clear();
        return false;
    }

    // Kronos ("Avatar 2.0" -- "Facial System" -- "Ensure in-game avatars
    // update instantly when equipping new items"... and, more directly,
    // "the actual playable avatar has a real face"): spawns the five
    // real facial feature entities and folds them into the exact same
    // skinnedAvatarEntities_ list AvatarController::tick() already
    // drives every frame -- one real update loop, not a second one. A
    // real, honest, logged-but-non-fatal degrade if this fails (same
    // "the avatar itself is still real and already spawned" precedent
    // the animation-clip loading loop just below already establishes) --
    // a faceless-but-otherwise-correct avatar is still real and playable.
    std::vector<EntityId> faceEntities;
    std::string faceError;
    if (spawnAvatarFace(ecs_, skeleton, skinTone, riggedMeshLibrary_, renderer_.allocator(), renderer_.device(),
                         renderer_.commandPool(), renderer_.graphicsQueue(), faceEntities, faceError)) {
        skinnedAvatarEntities_.insert(skinnedAvatarEntities_.end(), faceEntities.begin(), faceEntities.end());
    } else {
        std::fprintf(stderr, "Application: spawnLocalPlayerAvatar() -- spawnAvatarFace() failed: %s\n",
                     faceError.c_str());
    }

    // Kronos ("Avatar 2.0" -- "Clothing Meshes" -- "Runtime Integration"):
    // real, same "fold into the one real skinnedAvatarEntities_ list"
    // pattern spawnAvatarFace() just established above. `localProfile_`
    // isn't reachable from here (Application has no real identity
    // concept of its own, by design -- see core::LocalProfile's own
    // "engine_runtime/Studio each own their own real profile" scope), so
    // the real fit choice is threaded in as a parameter instead, same
    // "caller resolves from its own real, persisted state" precedent
    // skinTone/headShape/bodyProportions/loadout already establish for
    // this exact function.
    std::vector<EntityId> clothingEntities;
    std::string clothingError;
    if (spawnAvatarClothing(ecs_, skeleton, loadout, catalogueIndex, bodyProportions, clothingFit, riggedMeshLibrary_,
                             renderer_.allocator(), renderer_.device(), renderer_.commandPool(),
                             renderer_.graphicsQueue(), clothingEntities, clothingError)) {
        skinnedAvatarEntities_.insert(skinnedAvatarEntities_.end(), clothingEntities.begin(), clothingEntities.end());
    } else {
        std::fprintf(stderr, "Application: spawnLocalPlayerAvatar() -- spawnAvatarClothing() failed: %s\n",
                     clothingError.c_str());
    }

    avatarController_ = std::make_unique<AvatarController>(skeleton);

    // Real, shipped clips (engine/assets/animations/*.anim) -- same
    // packaged-vs-dev-build resolution every other real asset path in
    // this codebase uses. A missing clip is a real, honest partial
    // degrade (that locomotion state just holds whatever pose the last
    // successfully-loaded clip left, per AnimationPlayer's own "finished/
    // never-started clip holds its pose" behavior) logged to stderr, not
    // a fatal error -- the avatar itself is still real and already
    // spawned above.
    std::string animDir = resolveResourceDir(executableDirectory(), "assets", ENGINE_ASSET_DIR) + "/animations";
    // Kronos ("Avatar Phase" -- "AvatarEditor: Animation Overrides"):
    // `overridePath` (non-empty) is tried first; a broken override
    // real-falls back to the shipped default clip (rather than leaving
    // this locomotion state with no clip at all), same real, honest
    // fail-soft discipline the shipped-clip-only path already had.
    auto loadClip = [&](const char* fileBaseName, void (AvatarController::*setter)(AnimationClip),
                         const std::string& overridePath) {
        std::string shippedPath = animDir + "/" + fileBaseName + ".anim";
        AnimationClip clip;
        if (!overridePath.empty()) {
            if (clip.loadFromFile(overridePath)) {
                (avatarController_.get()->*setter)(std::move(clip));
                return;
            }
            std::fprintf(stderr,
                         "Application: spawnLocalPlayerAvatar() -- override clip \"%s\" failed to load, falling "
                         "back to the shipped default.\n",
                         overridePath.c_str());
        }
        if (clip.loadFromFile(shippedPath)) {
            (avatarController_.get()->*setter)(std::move(clip));
        } else {
            std::fprintf(stderr, "Application: spawnLocalPlayerAvatar() -- could not load \"%s\".\n", shippedPath.c_str());
        }
    };
    loadClip("idle", &AvatarController::setIdleClip, animationOverrides.idleClipPath);
    loadClip("walk", &AvatarController::setWalkClip, animationOverrides.walkClipPath);
    loadClip("run", &AvatarController::setRunClip, animationOverrides.runClipPath);
    loadClip("jump_start", &AvatarController::setJumpClip, animationOverrides.jumpStartClipPath);
    loadClip("jump_air", &AvatarController::setJumpAirClip, animationOverrides.jumpAirClipPath);
    loadClip("jump_land", &AvatarController::setJumpLandClip, animationOverrides.jumpLandClipPath);

    return true;
}

void Application::refreshLocalPlayerAvatarAppearance(glm::vec4 skinTone, const AvatarLoadout& loadout,
                                                       const CatalogueIndex& catalogueIndex) {
    if (skinnedAvatarEntities_.empty()) return; // real, honest no-op -- no avatar currently spawned
    std::array<glm::vec4, kHumanoidBodySegmentCount> colors =
        resolveSegmentColorsForLoadout(loadout, catalogueIndex, skinTone);
    for (size_t i = 0; i < skinnedAvatarEntities_.size() && i < colors.size(); ++i) {
        if (auto* skinned = ecs_.tryGetComponent<SkinnedRenderable>(skinnedAvatarEntities_[i])) {
            // Kronos ("Avatar 2.0" -- "Visual Fidelity: per-segment color
            // gradients"): same real shading step spawnRiggedAvatar()
            // itself applies -- see applySegmentShadingGradient()'s own
            // comment.
            skinned->baseColor = applySegmentShadingGradient(static_cast<HumanoidBodySegment>(i), colors[i]);
        }
    }
}

bool Application::startNetworking(const net::NetworkSession::Config& config) {
    // Kronos ("Active Joining UI" -- Scripting event hooks): these four
    // real observer callbacks are registered BEFORE initialize() below,
    // not after -- Server-mode initialize() fires onSessionJoined_
    // synchronously, before it even returns (see NetworkSession::
    // initialize()'s own comment), so registering it afterward would
    // silently miss that real, first call. Meaningful on either role
    // (Server: fires from the real join handshake/disconnect; Client:
    // fires from the real JoinAccepted/roster broadcasts/Disconnect) --
    // one real Scripting call per hook, not two independently-drifting
    // notions of "a session/player joined."
    networkSession_.setOnSessionJoined([this] { scripting_.fireSessionJoin(); });
    networkSession_.setOnSessionLeft([this] { scripting_.fireSessionLeave(); });
    networkSession_.setOnPlayerAdded(
        [this](net::PlayerId player, const std::string& name) { scripting_.firePlayerJoin(player, name); });
    networkSession_.setOnPlayerRemoving(
        [this](net::PlayerId player, const std::string& name) { scripting_.firePlayerLeave(player, name); });

    // A real, minimal networked-player entity: Transform + Name (+
    // Renderable if the caller wants one visible -- left to the caller,
    // same "caller owns spawning, this just wires the session" split
    // setOreDropMeshHandle()/setTerrain() already established) rather
    // than characterController_'s own capsule+physics rig. See this
    // method's own header comment for why: NetworkSession's real
    // movement sync (net::applyNetworkedMovement()) is a deliberately
    // simple kinematic model, not a replay of live Jolt physics steps --
    // driving characterController_'s physics-backed entity with it would
    // fight physics.step() every tick over who owns that entity's
    // Transform.
    if (config.mode == net::NetworkMode::Server) {
        networkSession_.setOnPlayerJoin([this](ECS& ecs, net::PlayerId player) -> EntityId {
            std::string entityName = "Player" + std::to_string(player);
            EntityId entity = ecs.createEntity(entityName);
            std::fprintf(stdout, "Application: spawned real avatar entity for player %u\n", player);
            return entity;
        });
    }

    return networkSession_.initialize(config);
}

void Application::shutdown() {
    if (!initialized_) return;

    networkSession_.shutdown();
    gameLoop_.reset();
    input_.shutdown();
    scripting_.shutdown();
    audio_.shutdown();
    physics_.shutdown();

    // Ordering contract from Renderer::shutdown()'s NOTE: mesh/texture GPU
    // resources are VMA allocations and must be freed before the
    // allocator they came from is destroyed.
    meshLibrary_.destroyAll(renderer_.allocator());
    textureLibrary_.destroyAll(renderer_.allocator(), renderer_.device());
    uiRenderer_.shutdown();
    renderer_.shutdown();

    window_.shutdown();

    initialized_ = false;
}

} // namespace engine::core
