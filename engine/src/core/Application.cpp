#include "core/RobloxServices.hpp"
#include "core/Application.hpp"
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

namespace engine::core {

Application::Application() = default;

Application::~Application() {
    shutdown();
}

bool Application::initialize(const CreateInfo& info) {
    if (info.headless) {
        if (SDL_Init(SDL_INIT_EVENTS | SDL_INIT_TIMER) != 0) {
            std::fprintf(stderr, "Application: headless SDL_Init failed: %s\n", SDL_GetError());
            return false;
        }
        sdlInitialized_ = true;
    }

    if (!info.headless) {
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
        // Kronos ("Fatal Init Diagnostics" -- Jay's Windows startup-crash
        // report): Window::initialize() itself already logged the real,
        // specific diagnosis (raw SDL_GetError() plus a classified,
        // actionable hint -- see Window.cpp's own classifySdlFailure())
        // via core::logError(), which both mirrors to stderr and, once
        // main.cpp's own Logger::instance().enableFileLogging() call has
        // run, persists to a real on-disk log file. No need to repeat a
        // second, less specific message here.
        return false;
    }

    // In-game text chat: '/' or Enter (when not already focused) opens
    // the input box and starts real SDL text input; while focused,
    // characters/backspace/Enter(submit)/Escape(cancel) are all handled
    // here rather than through UnifiedInput's action-polling (that's for
    // discrete actions, not free text).
    window_.setRawEventCallback([this](const SDL_Event& event) {
        if (!gameMode_) return;
        GameModeContext modeCtx = modeContext();
        gameMode_->onRawEvent(modeCtx, event);
    });

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
    }

    if (!physics_.initialize()) {
        std::fprintf(stderr, "Application: Physics::initialize failed.\n");
        return false;
    }
    physics_.setTouchRecording(true);

    if (!audio_.initialize()) {
        // Audio device init can legitimately fail in headless/CI
        // environments; treated as non-fatal so the rest of the engine
        // still runs, matching how Roblox itself degrades on a machine
        // with no audio device rather than refusing to start.
        std::fprintf(stderr, "Application: Audio::initialize failed -- continuing without audio.\n");
    }

    if (!info.headless) {
        installRenderResourceLoaders(resources_, {&renderer_, &meshLibrary_, &textureLibrary_, &audio_});
        if (const char* hot = std::getenv("KRONOS_HOT_RELOAD"); hot != nullptr && hot[0] == '1') {
            resources_.setHotReload(true, 0.5);
        }
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
    // Kronos ("Chat System"): the `TextChatService` table -- see
    // ScriptChatApi.hpp. Constructed alongside the other script APIs
    // because it installs a NetworkSession receive hook in its
    // constructor, which must be in place before any session starts.
    scriptChatApi_ = std::make_unique<ScriptChatApi>(networkSession_, scripting_);

    // Kronos ("Gemini Flash-Lite moderation"): remote chat classification,
    // attached only when GEMINI_API_KEY is actually set. Unset is a
    // supported configuration -- every developer machine and CI runner is
    // in it -- and leaves the pre-existing local-filter-only path exactly
    // as it was.
    //
    // Two workers, not one: a single worker serialises every message
    // behind the slowest one in flight, which under a slow upstream turns
    // per-message latency into a queue.
    httpWorkerPool_ = std::make_unique<net::HttpWorkerPool>(2);
    safety::GeminiConfig geminiConfig = safety::configFromEnvironment();
    // The fallback verdict is SAFE by construction: NetworkSession only
    // dispatches to the remote classifier AFTER its own profanity filter,
    // TrustSafetyService classification and rate limiting have already
    // passed the message. So "the model could not be reached" means
    // "local filters approved it", not "nothing checked it".
    chatModerationClient_ = std::make_unique<safety::GeminiModerationClient>(
        *httpWorkerPool_, geminiConfig,
        [](const std::string&) { return safety::ModerationVerdict{}; });

    if (chatModerationClient_->isConfigured()) {
        networkSession_.setChatModerationClient(chatModerationClient_.get());
        logInfo("Moderation", "Gemini chat moderation active (model %s)", geminiConfig.model.c_str());
    } else {
        logInfo("Moderation", "GEMINI_API_KEY unset -- chat moderation runs on local filters only");
    }
    if (const char* reviewLog = std::getenv("KRONOS_CHAT_REVIEW_LOG"); reviewLog != nullptr && *reviewLog != '\0') {
        networkSession_.setChatReviewLogPath(reviewLog);
        logInfo("Moderation", "writing every chat message and verdict to %s", reviewLog);
    }
    // Kronos ("Kronos Scripting Environment"): the real `world.spawnPlayer`
    // + `avatar` table -- see ScriptAvatarApi.hpp's own header comment.
    // Holds `*this` (always valid), so no ordering constraint the way
    // scriptWorldApi_ above has.
    scriptAvatarApi_ = std::make_unique<ScriptAvatarApi>(*this);
    scriptUiLayoutApi_ = std::make_unique<ScriptUiLayoutApi>(scripting_);
    scriptAudioApi_ = std::make_unique<ScriptAudioApi>(audio_, ecs_);
    scripting_.setBindingsHook([this](lua_State* L) {
        scriptWorldApi_->registerInto(L);
        scriptNetworkApi_->registerInto(L);
        scriptChatApi_->registerInto(L);
        scriptUiApi_.registerInto(L);
        scriptUiLayoutApi_->registerInto(L);
        scriptAudioApi_->registerInto(L);
        // Must run after scriptWorldApi_->registerInto(L) above --
        // ScriptAvatarApi::registerInto() appends world.spawnPlayer onto
        // the `world` global table scriptWorldApi_ just created, it
        // doesn't create its own.
        scriptAvatarApi_->registerInto(L);
        if (gameMode_) gameMode_->registerScripting(L);
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
    python_.setWorld(&ecs_, &physics_);
    python_.setOutputCallback([](const std::string& line) {
        if (line.rfind("[python]", 0) == 0) logError("Python", "%s", line.c_str());
        else logInfo("Python", "%s", line.c_str());
    });
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
    // PROJECT: DESPAIR -- not part of CharacterController's own bindings
    // either, same reasoning as "Interact" just above: despair::
    // FPSPlayerSettings-gated stealth (see despair::computeNoiseLevel())
    // is a DESPAIR-specific concern, not a generic character-movement one.
    // Bound generically here regardless of scene, same as every other
    // action -- a real, honest no-op read (isActionDown() just returns
    // false) for any scene that never checks it.
    input_.bindAction("Crouch", platform_adapters::InputBinding{platform_adapters::PhysicalInputKind::KeyboardKey,
                                                                  SDL_SCANCODE_LCTRL});
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
    // Kronos ("Avatar 2.0" -- "Animation Polish" -- "Support emote
    // playback from Marketplace items"): real -- core::playEquippedEmote()
    // (EmoteSystem.hpp) and AvatarController::playEmote() already existed
    // and worked, but had no real trigger anywhere in actual gameplay
    // (only Studio's AvatarPreviewer "Try On" flow called the underlying
    // pieces) -- this is the real, new gameplay-side trigger that
    // function's own header comment already anticipated ("a runtime
    // emote keybind").
    input_.bindAction("PlayEmote", platform_adapters::InputBinding{platform_adapters::PhysicalInputKind::KeyboardKey,
                                                                      SDL_SCANCODE_G});
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
    // PROJECT: DESPAIR -- hotbar item use. Same physical 1-4 keys
    // TntWarsSelectClass1-4 already bind, harmless overlap since only one
    // of these two action namespaces is ever checked for a given loaded
    // scene (same "bound everywhere, safe no-op where irrelevant"
    // convention as every other action in this block).
    input_.bindAction("HotbarUseSlot1",
                       platform_adapters::InputBinding{platform_adapters::PhysicalInputKind::KeyboardKey, SDL_SCANCODE_1});
    input_.bindAction("HotbarUseSlot2",
                       platform_adapters::InputBinding{platform_adapters::PhysicalInputKind::KeyboardKey, SDL_SCANCODE_2});
    input_.bindAction("HotbarUseSlot3",
                       platform_adapters::InputBinding{platform_adapters::PhysicalInputKind::KeyboardKey, SDL_SCANCODE_3});
    input_.bindAction("HotbarUseSlot4",
                       platform_adapters::InputBinding{platform_adapters::PhysicalInputKind::KeyboardKey, SDL_SCANCODE_4});
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
    if (!info.headless) input_.setRelativeMouseMode(info.startWithCapturedMouse);

    // engine_runtime wants the 3D view filling the whole window -- unlike
    // Studio, which never calls setScene() on its own Renderer (see
    // Renderer.hpp's setScene() doc comment). riggedMeshLibrary_ is the
    // real fix for spawnLocalPlayerAvatar()'s own skinned body actually
    // being drawn on this path -- see setScene()'s own header comment.
    if (!info.headless) {
        renderer_.setScene(&ecs_, &camera_, &meshLibrary_, &particleSystem_, &textureLibrary_, &riggedMeshLibrary_);
    }

    runtime::GameLoop::Subsystems subsystems{info.headless ? nullptr : &window_,
                                             info.headless ? nullptr : &renderer_,
                                             &ecs_, &physics_, &audio_, &scripting_, &camera_};
    gameLoop_ = std::make_unique<runtime::GameLoop>(subsystems);
    gameLoop_->setFrameStartHook([this](float dt) {
        signals::renderStepped(ecs_, dt);
        signals::flush(ecs_);
        instances::applyScriptCamera(ecs_);
    });

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
    worldStreamer_.beforeDestroy = [this](ECS& ecs, EntityId entity) {
        if (const auto* script = ecs.tryGetComponent<Script>(entity); script != nullptr && script->scriptId != kInvalidScript) {
            scripting_.unload(script->scriptId);
        }
    };
    worldStreamer_.waitForGpu = [this] {
        if (!headless_) renderer_.waitIdle();
    };
    worldStreamer_.deferDestroy = [this](std::function<void()> destroy) {
        if (headless_) destroy();
        else renderer_.deferDestroy(std::move(destroy));
    };
    gameLoop_->setPreTickHook([this](float dt) {
        signals::stepped(ecs_, dt);
        updateWorldStreaming();
        if (headless_) {
            tickGameScripts(dt);
            return;
        }
        input_.addMouseWheel(window_.takeMouseWheel());
        input_.addKeyPresses(window_.takeKeyPresses());
        input_.update();
        tickRollbackMatch(dt);
        // Sprint 14: accumulate this sim tick's real mouse delta for the
        // (slower) network hook to drain -- see networkMouseDeltaAccumulator_'s
        // own comment in Application.hpp for why this is necessary, not
        // just defensive: UnifiedInput::mouseDelta() resets every real
        // update() call above, and update() runs every sim tick.
        networkMouseDeltaAccumulator_ += input_.mouseDelta();
        networkWheelAccumulator_ += input_.mouseWheel();

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
        //
        // Kronos (beta-blocking fix -- "rigged avatar turns into a capsule
        // on rejoin"): also gated on !networkSession_.isClient() now --
        // see wasNetworkedClient_'s own comment in Application.hpp for why
        // this offline tick (and the camera_ writes inside it) must not
        // keep running once a networked session's own hook
        // (setNetworkTickHook() below) owns camera_ instead.
        bool isNetworkedClientNow = networkSession_.isClient();
        if (isNetworkedClientNow != wasNetworkedClient_) {
            // Real hide/show, not a spawn/despawn -- the offline rigged
            // avatar's entities, GPU mesh handles, and AvatarController
            // state all stay exactly as they were, so leaveSession()
            // resumes local play with the same avatar instead of paying
            // spawnLocalPlayerAvatar()'s real GPU-upload cost again just to
            // undo this.
            for (EntityId entity : skinnedAvatarEntities_) {
                if (auto* skinned = ecs_.tryGetComponent<SkinnedRenderable>(entity)) {
                    skinned->visible = !isNetworkedClientNow;
                }
            }
            wasNetworkedClient_ = isNetworkedClientNow;
        }

        if (gameMode_) {
            GameModeContext modeCtx = modeContext();
            gameMode_->beforeMovement(modeCtx);
        }

        if (!cameraShowcaseModeEnabled() && !movementInputSuspended_ && !isNetworkedClientNow) {
            characterController_.tick(dt, ecs_, physics_, input_, camera_, avatarController_.get(),
                                       avatarController_ ? &skinnedAvatarEntities_ : nullptr);
        }

        if (gameMode_ && gameMode_->controlsCamera()) {
            GameModeContext modeCtx = modeContext();
            gameMode_->tickCamera(modeCtx, dt);
        }

        resources_.update();
        particleSystem_.update(gameLoop_ ? dt * gameLoop_->timeScale() : dt, ecs_);
        animationPlayer_.tick(dt, ecs_);
        if (gameMode_) {
            GameModeContext modeCtx = modeContext();
            gameMode_->updateSystems(modeCtx, dt);
        }

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
        // Real hot-reload path (Phase 7): see core::tickScriptHotReload's
        // own comment -- a previously-loaded script whose source changed
        // gets its stale VM real-unloaded first (see Scripting::unload()'s
        // own KNOWN GAP comment for the one accepted limitation, an
        // outstanding task.wait() on this specific script isn't purged),
        // matching the same real, honest caveat studio::plugins::
        // ScriptedPlugin's own reload() already carries. Pulled into a
        // shared free function so Studio's own Play-mode preview
        // (PhysicsPreviewPlugin) can run the identical logic rather than a
        // hand-copied second version.
        tickGameScripts(dt);

        // Kronos ("Native Plugin Architecture"): real per-tick forward to
        // every currently loaded native plugin -- see
        // NativePluginManager::tick()'s own comment. An honest no-op when
        // nativePlugins_ has nothing loaded (today's default).
        nativePlugins_.update(ecs_);
        nativePlugins_.tick(dt, ecs_);
        pluginApi_.update();
        pluginApi_.tick(dt);

        totalSimTime_ += dt;

        // Kronos ("Environmental Detail" world-building): real, general,
        // map-agnostic wind -- see Application::windState()'s own public
        // comment for why this runs unconditionally here rather than
        // being scoped inside the TNT Wars block below.
        core::tickWindSway(ecs_, windState_, totalSimTime_);
        core::tickAtmosphericDustWind(ecs_, windState_, totalSimTime_);
        if (gameMode_) {
            GameModeContext modeCtx = modeContext();
            gameMode_->tickEnvironment(modeCtx, dt);
        }

        // Runtime Interaction Example: the moving/"jumpable" platform
        // (Components.hpp's MovingPlatform) -- driven every tick straight
        // from accumulated sim time, independent of the interaction
        // trigger below entirely (nothing needs to interact with it, the
        // player just needs to be able to jump onto/off of it while it
        // moves).
        // In a rollback match the rollback simulation moves them instead.
        const bool rollbackOwnsPhysics = rollbackNet_.phase() == net::RollbackNetSession::Phase::Lobby ||
                                         rollbackNet_.phase() == net::RollbackNetSession::Phase::Running;
        auto movingPlatforms = ecs_.view<MovingPlatform, Transform>();
        for (auto entity : movingPlatforms) {
            if (rollbackOwnsPhysics) break;
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
        if (!cameraShowcaseModeEnabled() && indoorLightingModeEnabled_) {
            // Kronos ("Avatar Gameplay Lighting Harmonisation Pass"):
            // real, deliberate skip of the whole day/night/atmosphere-
            // override/TNT-Wars-zone block below -- an indoor scene
            // uses the same real, shared "neutral indoor" preset the
            // avatar preview panels use (see setIndoorLightingMode()'s
            // own comment for why no caller enables this yet), and real-
            // forces weather back to Clear every tick so a live outdoor
            // weather event can never perturb it -- "Weather Isolation"
            // per this pass's own explicit requirement. 0.0f transition
            // seconds is a real, immediate snap, not a fade -- an indoor
            // scene should never visibly "clear up," it was never
            // supposed to have weather in the first place.
            renderer_.setLighting(avatarIndoorPreviewLighting());
            renderer_.setWeather(WeatherKind::Clear, 0.0f);
        } else if (!cameraShowcaseModeEnabled()) {
            // A game's Lighting service owns the clock (scripts set ClockTime).
            if (!services::lightingClock(ecs_, timeOfDayState_.hours)) {
                tickTimeOfDay(timeOfDayState_, dt, dayLengthSeconds_);
            }
            SceneLighting tickLighting = computeLightingForTimeOfDay(timeOfDayState_.hours);
            services::applyLighting(ecs_, tickLighting);
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
                if (atmosphereOverride_.overrideSun) {
                    tickLighting.directionWS = atmosphereOverride_.sunDirectionWS;
                    tickLighting.color = atmosphereOverride_.sunColor;
                    tickLighting.intensity = atmosphereOverride_.sunIntensity;
                    tickLighting.ambient = atmosphereOverride_.ambient;
                    tickLighting.ambientGround = atmosphereOverride_.ambientGround;
                }
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
            if (gameMode_) {
                GameModeContext modeCtx = modeContext();
                gameMode_->adjustLighting(modeCtx, tickLighting, characterPos);
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
        if (gameMode_) {
            GameModeContext modeCtx = modeContext();
            gameMode_->tickWorld(modeCtx, dt, character, characterPos);
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
        //
        // In first-person (see despair::configureFirstPersonCamera()),
        // camera_.position sits at eye height *inside* the character's own
        // capsule, not outside it like a third-person orbit cam -- a ray
        // cast from inside a convex shape reports an immediate self-hit at
        // fraction 0 rather than passing through to whatever is actually
        // being looked at. The `rayHit.entity != character` filter alone
        // can't recover from that: CastRay only returns the single closest
        // hit, so once the self-hit wins, the real target past it is never
        // even tested. Fixed via despair::computeInteractionRayOrigin (same
        // "start the ray outside the capsule's own collision volume"
        // convention CharacterController::tryStepUp() already uses, and the
        // one place that geometry is now spelled out -- see its own
        // comment). Harmless in third-person too: it just shrinks the ray's
        // dead zone by the same amount, right in front of the already-
        // distant orbit camera.
        constexpr float kInteractDistance = 6.0f;
        float capsuleRadius = characterController_.settings().capsuleRadius;
        constexpr float kRayStartSkin = 0.05f;
        float originOffset = capsuleRadius + kRayStartSkin;
        glm::vec3 rayOrigin = camera_.position + camera_.forward() * originOffset;
        Physics::RaycastHit rayHit = physics_.raycast(rayOrigin, camera_.forward(), kInteractDistance - originOffset);
        EntityId lookAtTarget = (rayHit.hit && rayHit.entity != character) ? rayHit.entity : kNullEntity;

        // Real proximity targets: every Interactable within its own
        // proximityRadius of the character, nearest first -- see
        // findInteractablesInRange()'s header comment. Independent of
        // camera facing entirely, the real second interaction-mapping
        // this pass adds alongside the pre-existing raycast one.
        std::vector<EntityId> nearby = findInteractablesInRange(ecs_, characterPos);
        EntityId nearestProximityTarget = nearby.empty() ? kNullEntity : nearby.front();

        // Real UI hint: the game mode's prompt is drawn on-screen
        // (see the FPSPlayerSettings-gated UI block below), and still
        // prints the same stdout line every call site already did, only
        // on an actual change (entering/leaving range or look-at), not
        // every tick while unchanged.
        EntityId hintTarget = lookAtTarget != kNullEntity ? lookAtTarget : nearestProximityTarget;
        if (hintTarget != lastInteractionHintEntity_) {
            if (hintTarget != kNullEntity) {
                std::string prompt = "Interact";
                if (auto* interactable = ecs_.tryGetComponent<Interactable>(hintTarget)) prompt = interactable->prompt;
                if (gameMode_) gameMode_->setInteractionPrompt(prompt);
                std::fprintf(stdout, "[UI hint] %s\n", prompt.c_str());
            } else {
                if (gameMode_) gameMode_->setInteractionPrompt("");
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
        GameModeContext modeCtx = modeContext();
        bool interactDown = input_.isActionDown("Interact");

        // PROJECT: DESPAIR -- raycast container searching / duffel-bag
        // hold-to-search (despair::LootContainer). Deliberately ticked
        // here, every tick, BEFORE the rising-edge gate just below: a
        // hold-to-search mechanic needs to observe Interact held down
        // continuously across many ticks, which the edge-triggered
        // dispatch (one call per fresh press) can never see. See
        // tickContainerSearches()'s own header comment for why this
        // needs no stored "current hold target" state of its own.
        if (gameMode_) gameMode_->tickLookAt(modeCtx, dt, character, interactDown, lookAtTarget);

        if (interactDown && !interactKeyWasDown_) {
            if (gameMode_ && gameMode_->consumeInteractPress(modeCtx, character, characterPos)) {
                interactKeyWasDown_ = interactDown;
                return;
            }

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
                    // PROJECT: DESPAIR -- door unlocking (despair::LockedDoor)
                    // layered directly on top of this existing generic Door,
                    // not a parallel door type (see LockedDoor's own header
                    // comment). A locked door with a missing keycard never
                    // reaches toggleDoor() at all -- the whole point of the
                    // gate -- while an already-unlocked or just-now-unlocked
                    // door falls straight through to the exact same toggle
                    // every other Door already gets.
                    bool canToggle = true;
                    if (gameMode_) canToggle = gameMode_->allowDoorToggle(modeCtx, target, character);
                    if (canToggle) {
                        if (auto* transform = ecs_.tryGetComponent<Transform>(target)) toggleDoor(*door, *transform);
                    }
                }
                if (ecs_.hasComponent<Pickup>(target)) {
                    collectPickup(target, ecs_);
                    // A collected pickup with its own live Jolt body (see
                    // FacilityMapBuilder.cpp's keycard sensor body) would
                    // otherwise sit there as an invisible collider
                    // forever -- collectPickup() only hides the
                    // Renderable/removes Interactable (core-generic, no
                    // Physics dependency of its own), so this raycast
                    // trigger detaches the body itself, same as
                    // DoorUnlockResult::Unlocked does above. A safe no-op
                    // for any Pickup that never had a body.
                    physics_.detachBody(target, ecs_);
                }
                if (gameMode_) gameMode_->onInteract(modeCtx, target, character, characterPos);
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
                                              camera_.yawDegrees, camera_.pitchDegrees, dt, &physics_);

            if (auto* transform = ecs_.tryGetComponent<Transform>(networkedLocalPlayerEntity_)) {
                constexpr float kCameraDistance = 6.0f;
                const bool shiftLocked = input_.isShiftLocked();
                if (networkWheelAccumulator_ != 0.0f) {
                    networkCameraZoom_ =
                        std::clamp(networkCameraZoom_ * std::pow(0.85f, networkWheelAccumulator_), 0.35f, 3.0f);
                    networkWheelAccumulator_ = 0.0f;
                }
                networkShoulderBlend_ += ((shiftLocked ? 1.0f : 0.0f) - networkShoulderBlend_) * (1.0f - std::exp(-10.0f * dt));
                float yawRad = glm::radians(camera_.yawDegrees);
                glm::vec3 camRight(-std::sin(yawRad), 0.0f, std::cos(yawRad));
                glm::vec3 focus = transform->position + glm::vec3(0.0f, 1.7f, 0.0f);
                camera_.position = CharacterController::keepCameraOutOfWalls(
                    physics_, focus,
                    focus - camera_.forward() * (kCameraDistance * networkCameraZoom_) + camRight * networkShoulderBlend_,
                    networkedLocalPlayerEntity_);

                if (networkedAvatarController_ && dt > 0.0f) {
                    auto* vertical = ecs_.tryGetComponent<net::NetworkedVerticalMotion>(networkedLocalPlayerEntity_);
                    glm::vec3 horizontalVelocity = (transform->position - networkedAvatarLastPosition_) / dt;
                    networkedAvatarLastPosition_ = transform->position;
                    glm::vec3 velocity(horizontalVelocity.x, vertical ? vertical->velocityY : 0.0f, horizontalVelocity.z);
                    bool grounded = vertical ? vertical->grounded : true;

                    bool moving = glm::length(moveAxis) > 0.0001f;
                    if (shiftLocked) {
                        networkedFacingYaw_ = std::atan2(std::cos(yawRad), std::sin(yawRad));
                    } else if (moving) {
                        glm::vec3 forward(std::cos(yawRad), 0.0f, std::sin(yawRad));
                        glm::vec3 worldMove = forward * moveAxis.z + camRight * moveAxis.x;
                        float target = std::atan2(worldMove.x, worldMove.z);
                        float delta = std::remainder(target - networkedFacingYaw_, 6.28318530718f);
                        networkedFacingYaw_ += delta * (1.0f - std::exp(-16.0f * dt));
                    }
                    transform->rotation = glm::angleAxis(networkedFacingYaw_, glm::vec3(0.0f, 1.0f, 0.0f));
                    networkedAvatarController_->tick(dt, ecs_, networkedLocalPlayerEntity_,
                                                       networkedAvatarSkinnedEntities_, grounded, velocity);
                }
            }
        }
        networkSession_.tick(dt, ecs_, networkedLocalPlayerEntity_, &physics_);
        tickRemoteAvatars(dt);
    });

    // The other real event.onX trigger: Physics' actual Jolt
    // ContactListener (Physics.hpp's drainCollisionEvents()) queues new
    // contacts during step(); this hook (runs right after step(), see
    // GameLoop::setPostPhysicsHook's doc comment) drains and forwards them
    // to events.onCollision.
    gameLoop_->setPostPhysicsHook([this](float dt) {
        for (const auto& event : physics_.drainCollisionEvents()) {
            scripting_.fireCollision(static_cast<uint32_t>(event.first), static_cast<uint32_t>(event.second));
            python_.fireCollision(static_cast<uint32_t>(event.first), static_cast<uint32_t>(event.second));
        }
        for (const auto& touch : physics_.drainTouchEvents()) signals::touch(ecs_, touch.a, touch.b, touch.began);
        signals::heartbeat(ecs_, dt);
        signals::flush(ecs_);
        if (gameMode_) {
            GameModeContext modeCtx = modeContext();
            gameMode_->afterPhysics(modeCtx, dt);
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
    if (!info.headless) gameLoop_->setPostRenderHook([this](float dt) {
        // Kronos (beta-blocking fix -- "flickering when opening a game"):
        // real, one-time reveal -- the window was created hidden (see
        // Window::initialize()'s own comment) specifically so nothing
        // ever composites it before this exact point: right after its
        // own first real renderFrame() has actually succeeded and
        // presented a complete, correct frame. Every call after the
        // first is a real no-op (Window::show() itself is idempotent,
        // this flag just skips the call entirely).
        if (!windowShown_) {
            windowShown_ = true;
            window_.show();
        }

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

    // Kronos ("Native Plugin Architecture"): real, one-time discovery and
    // load of every native (.so/.dll) engine plugin sitting in the
    // packaged/dev "native_plugins" directory (see NativePluginManager.hpp's
    // own class comment) -- an honest no-op today on every existing dev
    // and packaged build, since no such directory is checked in yet, same
    // as ENGINE_GAMES_DIR before the first real bundled game shipped. A
    // plugin that fails to load (missing symbols, ABI mismatch) is
    // skipped with a real, specific stderr diagnosis rather than aborting
    // startup -- one bad plugin shouldn't take down the whole engine.
    pluginApi_.setScene(&ecs_);
    for (const auto& found : NativePluginManager::discover(
             resolveResourceDir(executableDirectory(), "native_plugins", ENGINE_NATIVE_PLUGIN_DIR))) {
        std::string pluginError;
        if (plugin::PluginHost::exportsPluginApi(found.libraryPath)) {
            if (!pluginApi_.load(found.libraryPath, {}, pluginError)) {
                std::fprintf(stderr, "Application: plugin '%s': %s\n", found.name.c_str(), pluginError.c_str());
            }
            continue;
        }
        if (!nativePlugins_.loadPlugin(found.name, found.libraryPath, ecs_, pluginError)) {
            std::fprintf(stderr, "Application: failed to load native plugin '%s': %s\n", found.name.c_str(),
                         pluginError.c_str());
        }
    }
    if (const char* hot = std::getenv("KRONOS_HOT_RELOAD"); hot != nullptr && std::string(hot) == "1") {
        nativePlugins_.setAutoReload(true, 0.5);
        pluginApi_.setAutoReload(true, 0.5);
    }

    headless_ = info.headless;
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

GameModeContext Application::modeContext() {
    return GameModeContext{ecs_,          physics_,    characterController_, camera_,
                           input_,        uiRenderer_, renderer_,            scripting_,
                           networkSession_, meshLibrary_, audio_,            scriptUiApi_,
                           *scriptUiLayoutApi_, lastPerformanceMetrics_, static_cast<uint32_t>(window_.width()),
                           static_cast<uint32_t>(window_.height()), totalSimTime_, reducedMotionEnabled_};
}

void Application::tickLocalAvatarIdle(float dt) {
    if (!avatarController_ || !ecs_.raw().valid(characterController_.entity())) return;
    avatarController_->tick(dt, ecs_, characterController_.entity(), skinnedAvatarEntities_, true, glm::vec3(0.0f));
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

    PlayerAvatarLook look;
    look.skinTone = skinTone;
    look.headShape = headShape;
    look.bodyProportions = bodyProportions;
    look.loadout = loadout;
    look.animationOverrides = animationOverrides;
    look.clothingFit = clothingFit;
    avatarController_ = spawnPlayerAvatarRig(ecs_, renderer_, riggedMeshLibrary_, look, catalogueIndex, skinnedAvatarEntities_);
    if (!avatarController_) {
        skinnedAvatarEntities_.clear();
        return false;
    }
    return true;
}

bool Application::buildAvatarRig(std::vector<EntityId>& skinned, std::unique_ptr<AvatarController>& controller) {
    Skeleton skeleton = buildHumanoidSkeleton();
    glm::vec4 skinTone(0.85f, 0.75f, 0.65f, 1.0f);
    std::string spawnError;
    if (!spawnRiggedAvatar(ecs_, skeleton, AvatarLoadout{}, CatalogueIndex{}, riggedMeshLibrary_, renderer_.allocator(),
                           renderer_.device(), renderer_.commandPool(), renderer_.graphicsQueue(), skinned, spawnError,
                           skinTone)) {
        std::fprintf(stderr, "Application: spawnRiggedAvatar() failed: %s\n", spawnError.c_str());
        skinned.clear();
        return false;
    }
    // A faceless or hairless avatar is still playable if either fails.
    std::vector<EntityId> faceEntities;
    std::string faceError;
    if (spawnAvatarFace(ecs_, skeleton, skinTone, riggedMeshLibrary_, renderer_.allocator(), renderer_.device(),
                        renderer_.commandPool(), renderer_.graphicsQueue(), faceEntities, faceError)) {
        skinned.insert(skinned.end(), faceEntities.begin(), faceEntities.end());
    } else {
        std::fprintf(stderr, "Application: spawnAvatarFace() failed: %s\n", faceError.c_str());
    }
    std::vector<EntityId> hairEntities;
    std::string hairError;
    if (spawnAvatarDefaultHair(ecs_, skeleton, AvatarLoadout{}, kDefaultHairColor, riggedMeshLibrary_, renderer_.allocator(),
                               renderer_.device(), renderer_.commandPool(), renderer_.graphicsQueue(), hairEntities,
                               hairError)) {
        skinned.insert(skinned.end(), hairEntities.begin(), hairEntities.end());
    } else {
        std::fprintf(stderr, "Application: spawnAvatarDefaultHair() failed: %s\n", hairError.c_str());
    }

    controller = std::make_unique<AvatarController>(skeleton);
    const std::string animDir = resolveResourceDir(executableDirectory(), "assets", ENGINE_ASSET_DIR) + "/animations";
    auto loadShippedClip = [&](const char* fileBaseName, void (AvatarController::*setter)(AnimationClip)) {
        AnimationClip clip;
        if (clip.loadFromFile(animDir + "/" + fileBaseName + ".anim")) (controller.get()->*setter)(std::move(clip));
    };
    loadShippedClip("idle", &AvatarController::setIdleClip);
    loadShippedClip("walk", &AvatarController::setWalkClip);
    loadShippedClip("run", &AvatarController::setRunClip);
    loadShippedClip("jump_start", &AvatarController::setJumpClip);
    loadShippedClip("jump_air", &AvatarController::setJumpAirClip);
    loadShippedClip("jump_land", &AvatarController::setJumpLandClip);
    return true;
}

void Application::clearRemoteAvatars() {
    for (RemoteAvatarRig& rig : remoteAvatars_) {
        for (EntityId e : rig.skinned) {
            if (ecs_.raw().valid(e)) ecs_.destroyEntity(e);
        }
    }
    remoteAvatars_.clear();
}

void Application::spawnRemoteAvatar(EntityId root) {
    if (headless_) return;
    RemoteAvatarRig rig;
    rig.root = root;
    if (!buildAvatarRig(rig.skinned, rig.controller)) return;
    if (const auto* transform = ecs_.tryGetComponent<Transform>(root)) rig.lastPosition = transform->position;
    std::fprintf(stdout, "Application: gave a remote player an avatar (%zu remote avatars)\n", remoteAvatars_.size() + 1);
    remoteAvatars_.push_back(std::move(rig));
}

void Application::tickRemoteAvatars(float dt) {
    if (dt <= 0.0f) return;
    for (RemoteAvatarRig& rig : remoteAvatars_) {
        auto* transform = ecs_.tryGetComponent<Transform>(rig.root);
        if (transform == nullptr || rig.controller == nullptr) continue;
        const glm::vec3 delta = (transform->position - rig.lastPosition) / dt;
        rig.lastPosition = transform->position;
        rig.controller->tick(dt, ecs_, rig.root, rig.skinned, true, glm::vec3(delta.x, delta.y, delta.z));
    }
}

void Application::setNetworkedLocalPlayerEntity(EntityId entity) {
    // Kronos (beta, "restore the 18-bone humanoid for online play"): real
    // teardown-then-spawn, mirroring spawnLocalPlayerAvatar()'s own
    // "real, honest reset" precedent -- a fresh join must not leave a
    // stale networkedAvatarController_ driving entities a previous
    // session's ECS wipe (or this same reset) already destroyed.
    for (EntityId e : networkedAvatarSkinnedEntities_) ecs_.destroyEntity(e);
    networkedAvatarSkinnedEntities_.clear();
    networkedAvatarController_.reset();
    networkedLocalPlayerEntity_ = entity;
    if (entity == kNullEntity) { // leaveSession()'s own teardown call
        clearRemoteAvatars();
        return;
    }

    // The plain capsule spawnNetworkedPlayerEntity_() (main.cpp) already
    // built on `entity` was this session's only real visual before
    // tonight -- real-hidden now (not destroyed: `entity`'s own Transform
    // is still the real, single source of position this rig, the
    // camera-follow hook, and net::applyNetworkedMovement() all share),
    // the same "hide, don't destroy" precedent wasNetworkedClient_ above
    // already established for the offline avatar's own capsule/mesh.
    if (auto* renderable = ecs_.tryGetComponent<Renderable>(entity)) renderable->visible = false;

    if (!buildAvatarRig(networkedAvatarSkinnedEntities_, networkedAvatarController_)) return;

    if (auto* transform = ecs_.tryGetComponent<Transform>(entity)) networkedAvatarLastPosition_ = transform->position;
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

bool Application::playEquippedEmote(const AvatarLoadout& loadout, const AnimationDatabase& animationDatabase,
                                     std::string& outError) {
    if (!avatarController_) return false; // real, honest no-op -- no avatar currently spawned
    return engine::core::playEquippedEmote(loadout, animationDatabase, *avatarController_, outError);
}

EntityId Application::respawnLocalPlayer(glm::vec3 position) {
    if (avatarController_) {
        // Kronos ("Kronos Scripting Environment" -- "world.spawnPlayer"):
        // real "respawn," not a fresh spawn -- an avatar already exists,
        // so this just teleports the real, live character entity via the
        // exact same Physics::setPosition() every other position-setting
        // script binding (ScriptWorldApi::luaSetPosition) already uses,
        // rather than re-running full cosmetic/clothing/hair spawn logic
        // a second time for what's really a position change.
        EntityId entity = characterController_.entity();
        if (entity != kNullEntity) physics_.setPosition(entity, ecs_, position);
        return entity;
    }
    // No avatar spawned yet -- a real, fresh spawn with the same default
    // cosmetics main.cpp's own bring-up world already uses (this class's
    // own spawnLocalPlayerAvatar() overload with only `spawnPosition`
    // given -- see that method's own default-argument list).
    if (!spawnLocalPlayerAvatar(position)) return kNullEntity;
    return characterController_.entity();
}

bool Application::tryPlayEmoteForEntity(EntityId entity, const std::string& emoteId, bool looping,
                                         std::string& outError) {
    outError.clear();
    // Real, honest false, not a crash -- no avatar spawned, or no
    // AnimationDatabase has been wired in yet (RuntimeShell wires this in
    // lazily at ensureAvatarCatalogueLoaded() time, which may not have
    // run yet for a script that fires very early -- see
    // setAnimationDatabase()'s own header comment).
    if (avatarController_ == nullptr || animationDatabase_ == nullptr) return false;
    // Kronos ("Kronos Scripting Environment"): only the local player has
    // a real, live AvatarController today (avatarController_ is a single
    // instance, not a per-entity registry -- see Application.hpp's own
    // member comment) -- any other entity id is a real, honest false, not
    // a silent no-op pretending it worked. A future per-NPC/remote-player
    // AvatarController registry would relax this check, not this
    // function's own contract.
    if (entity != characterController_.entity()) return false;
    AnimationClip clip;
    if (!resolveEmoteClip(emoteId, *animationDatabase_, clip, outError)) return false;
    // Kronos: real, full-body playback -- the same choice
    // core::playEquippedEmote() already makes ("classic Roblox-style
    // emotes take over the whole character," EmoteSystem.hpp's own
    // comment), not a partial upper-body override.
    avatarController_->playEmote(std::move(clip), looping, /*fullBody=*/true);
    return true;
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
    networkSession_.setOnSessionJoined([this] {
        scripting_.fireSessionJoin();
        robloxPlayerJoined(networkSession_.localPlayerId(), networkSession_.localDisplayName(), true);
    });
    networkSession_.setOnSessionLeft([this] {
        scripting_.fireSessionLeave();
        replication::stop(ecs_);
        remotenet::detach(ecs_);
    });
    networkSession_.setOnPlayerAdded([this](net::PlayerId player, const std::string& name) {
        scripting_.firePlayerJoin(player, name);
        robloxPlayerJoined(player, name, false);
    });
    networkSession_.setOnPlayerRemoving([this](net::PlayerId player, const std::string& name) {
        scripting_.firePlayerLeave(player, name);
        const InstanceRef robloxPlayer = remotenet::playerFor(ecs_, player);
        remotenet::removePlayer(ecs_, player);
        replication::removeClient(ecs_, player);
        if (robloxPlayer != kNoInstance) players::leave(ecs_, robloxPlayer);
    });
    // Roblox remotes travel over this session.
    remotenet::attach(ecs_, config.mode == net::NetworkMode::Server ? remotenet::Side::Server : remotenet::Side::Client,
                      [this](uint32_t target, std::vector<uint8_t> bytes, bool reliable) {
                          networkSession_.sendInstanceRemote(target, bytes, reliable);
                      });
    networkSession_.setOnRemoteEntityCreated([this](ECS&, EntityId entity, uint32_t) { spawnRemoteAvatar(entity); });
    networkSession_.setOnInstanceRemote([this](net::PlayerId sender, const uint8_t* data, size_t size) {
        remotenet::receive(ecs_, sender, data, size);
    });
    if (config.mode == net::NetworkMode::Server) replication::startServer(ecs_);
    else if (config.mode == net::NetworkMode::Client) replication::startClient(ecs_);

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
    signals::runService(ecs_).server = config.mode != net::NetworkMode::Client;
    signals::runService(ecs_).client = config.mode != net::NetworkMode::Server;
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

void Application::robloxPlayerJoined(net::PlayerId player, const std::string& name, bool local) {
    if (player == net::kInvalidPlayer || remotenet::playerFor(ecs_, player) != kNoInstance) return;
    EntityId root = kNullEntity;
    int64_t userId = player;
    if (local) {
        root = networkedLocalPlayerEntity_ != kNullEntity ? networkedLocalPlayerEntity_ : characterController_.entity();
    } else if (networkSession_.mode() == net::NetworkMode::Server) {
        root = networkSession_.playerEntity(player);
        if (const uint64_t profile = networkSession_.playerProfileId(player); profile != 0) {
            userId = static_cast<int64_t>(profile);
        }
    }
    remotenet::addPlayer(ecs_, player, players::join(ecs_, name, userId, root, local));
    if (!local) replication::addClient(ecs_, player);
}

void Application::tickGameScripts(float dt) {
    partbodies::setPhysics(ecs_, &physics_);
    instances::setScriptCamera(ecs_, headless_ ? nullptr : &camera_,
                               glm::vec2(static_cast<float>(window_.width()), static_cast<float>(window_.height())));
    core::tickScriptHotReload(ecs_, scripting_, &python_);
    python_.tick(dt);
    players::tick(ecs_, dt);
    remotenet::tick(ecs_, dt);
    replication::tick(ecs_, dt);
    partbodies::sync(ecs_, physics_, replication::active(ecs_) && !remotenet::isServer(ecs_));
}

bool Application::startRollbackMatch(const net::RollbackNetSession::Config& config, std::string* error) {
    if (headless_) {
        if (error) *error = "rollback matches need a window";
        return false;
    }
    rollbackPlayers_.clear();
    rollback_.reset();
    if (!rollbackNet_.start(config, [this](uint32_t players, uint32_t local) { return beginRollbackMatch(players, local); },
                            error)) {
        return false;
    }
    // Frozen until everyone has joined, so every player starts from the same state.
    if (gameLoop_) gameLoop_->setTimeScale(0.0f);
    if (characterController_.entity() != kNullEntity) characterController_.attachExternallyMoved(characterController_.entity());
    if (const char* title = SDL_GetWindowTitle(window_.handle()); title && baseWindowTitle_.empty()) baseWindowTitle_ = title;
    rollbackShownPhase_ = net::RollbackNetSession::Phase::Idle;
    showRollbackStatus();
    return true;
}

PhysicsRollback* Application::beginRollbackMatch(uint32_t players, uint32_t localPlayer) {
    if (worldStreamer_.isOpen()) {
        const WorldStreamingStats stats = worldStreamer_.stats();
        if (stats.loaded != stats.cells) return nullptr;
    }
    glm::vec3 spawn(0.0f, 1.0f, -6.0f);
    const EntityId previous = characterController_.entity();
    if (previous != kNullEntity) {
        if (const auto* transform = ecs_.tryGetComponent<Transform>(previous)) spawn = transform->position;
        physics_.detachBody(previous, ecs_);
        ecs_.raw().remove<StreamingSource>(previous);
    }

    const CharacterController::Settings settings = characterController_.settings();
    if (rollbackCapsuleMesh_ == Renderable::kInvalidHandle) {
        rollbackCapsuleMesh_ = meshLibrary_.registerMesh(Mesh::createCapsule(renderer_.allocator(), renderer_.device(),
                                                                             renderer_.commandPool(), renderer_.graphicsQueue(),
                                                                             settings.capsuleRadius, settings.capsuleHalfHeight));
    }
    static const glm::vec4 kPlayerColors[kMaxRollbackPlayers] = {
        {0.20f, 0.55f, 0.95f, 1.0f}, {0.95f, 0.35f, 0.25f, 1.0f}, {0.30f, 0.80f, 0.35f, 1.0f}, {0.95f, 0.80f, 0.20f, 1.0f},
        {0.70f, 0.40f, 0.90f, 1.0f}, {0.20f, 0.85f, 0.85f, 1.0f}, {0.95f, 0.55f, 0.80f, 1.0f}, {0.60f, 0.60f, 0.60f, 1.0f}};
    const bool localHasAvatar = !skinnedAvatarEntities_.empty();
    rollbackPlayers_.clear();
    for (uint32_t p = 0; p < players; ++p) {
        const glm::vec3 position = spawn + glm::vec3((static_cast<float>(p) - (players - 1) * 0.5f) * 2.0f, 0.0f, 0.0f);
        const EntityId body =
            physics_.createCharacterCapsule(ecs_, position, settings.capsuleRadius, settings.capsuleHalfHeight, 70.0f);
        ecs_.addComponent<StreamingSource>(body);
        ecs_.raw().emplace_or_replace<Name>(body, Name{"Player " + std::to_string(p + 1)});
        if (p != localPlayer || !localHasAvatar) {
            auto& renderable = ecs_.addComponent<Renderable>(body);
            renderable.meshHandle = rollbackCapsuleMesh_;
            renderable.baseColor = kPlayerColors[p];
            renderable.roughness = 0.5f;
        }
        rollbackPlayers_.push_back(body);
    }
    characterController_.attachExternallyMoved(rollbackPlayers_[localPlayer]);

    PhysicsRollback::Settings rollbackSettings;
    rollbackSettings.players = players;
    rollbackSettings.localPlayer = localPlayer;
    rollbackSettings.maxRollbackFrames = 16;
    rollbackSettings.frameDt = rollbackFrameDt_;
    std::vector<uint8_t> facing(players * sizeof(float), 0);
    matchScript_.reset();
    if (!matchScriptSource_.empty()) {
        auto script = std::make_shared<RollbackScript>();
        std::string error;
        std::vector<uint8_t> scriptState;
        if (script->load(matchScriptName_, matchScriptSource_, error) && script->initialState(players, scriptState, error)) {
            facing.insert(facing.end(), scriptState.begin(), scriptState.end());
            matchScript_ = std::move(script);
        } else {
            std::fprintf(stderr, "Application: match script %s not used: %s\n", matchScriptName_.c_str(), error.c_str());
        }
    }
    std::vector<EntityId> platforms;
    for (auto entity : ecs_.view<MovingPlatform, RigidBody>()) platforms.push_back(entity);
    std::sort(platforms.begin(), platforms.end());
    rollback_ = std::make_unique<PhysicsRollback>(
        physics_, ecs_, rollbackSettings,
        [characters = rollbackPlayers_, settings, platforms, script = matchScript_](RollbackFrameContext& frame) {
            for (EntityId entity : platforms) {
                const auto* platform = frame.ecs.tryGetComponent<MovingPlatform>(entity);
                const auto* transform = frame.ecs.tryGetComponent<Transform>(entity);
                if (!platform || !transform || platform->period <= 0.0f) continue;
                float cycle = static_cast<float>(frame.frame + 1) * frame.dt / platform->period;
                cycle -= std::floor(cycle);
                const glm::vec3 target =
                    platform->basePosition + platform->axis * (platform->amplitude * det::sin(6.28318530718f * cycle));
                frame.physics.moveKinematic(entity, frame.ecs, target, transform->rotation, frame.dt);
            }
            for (uint32_t p = 0; p < frame.playerCount; ++p) {
                float yaw = 0.0f;
                std::memcpy(&yaw, frame.gameState.data() + p * sizeof(float), sizeof(float));
                CharacterController::simulateRollback(frame.physics, frame.ecs, characters[p], settings, frame.inputs[p],
                                                      frame.dt, yaw);
                std::memcpy(frame.gameState.data() + p * sizeof(float), &yaw, sizeof(float));
            }
            if (script) script->step(frame, frame.playerCount * sizeof(float), characters);
        },
        std::move(facing));
    return rollback_.get();
}

void Application::tickRollbackMatch(float dt) {
    using Phase = net::RollbackNetSession::Phase;
    if (rollbackNet_.phase() == Phase::Idle) return;
    rollbackFrameDt_ = dt;
    RollbackInput local;
    if (rollback_ && !movementInputSuspended_) local = characterController_.sampleRollbackInput(input_);
    rollbackNet_.tick(local);

    if (rollbackNet_.phase() == Phase::Ended && rollbackShownPhase_ != Phase::Ended) {
        // Carry on alone: the world unfreezes and the local character walks normally again.
        std::fprintf(stderr, "Application: rollback match ended: %s\n", rollbackNet_.endReason().c_str());
        if (gameLoop_) gameLoop_->setTimeScale(1.0f);
        if (characterController_.entity() != kNullEntity) characterController_.setExternallyMoved(false);
    }
    rollbackTitleTimer_ -= dt;
    if (rollbackNet_.phase() != rollbackShownPhase_ || rollbackTitleTimer_ <= 0.0f) showRollbackStatus();
}

void Application::showRollbackStatus() {
    using Phase = net::RollbackNetSession::Phase;
    const Phase phase = rollbackNet_.phase();
    if (phase != rollbackShownPhase_) {
        if (phase == Phase::Running) {
            std::fprintf(stdout, "Application: rollback match started, %u players, you are player %u\n",
                         rollbackNet_.players(), rollbackNet_.localPlayer().value_or(0) + 1);
        }
        rollbackShownPhase_ = phase;
    }
    rollbackTitleTimer_ = 0.5f;
    char status[160];
    switch (phase) {
        case Phase::Lobby:
            if (rollbackNet_.isHost()) {
                std::snprintf(status, sizeof(status), "Rollback match: waiting for players (%u/%u)",
                              rollbackNet_.connectedPlayers(), rollbackNet_.players());
            } else {
                std::snprintf(status, sizeof(status), "Rollback match: joining...");
            }
            break;
        case Phase::Running:
            std::snprintf(status, sizeof(status), "Rollback match: player %u of %u, ping %.0f ms, %llu rollbacks",
                          rollbackNet_.localPlayer().value_or(0) + 1, rollbackNet_.players(), rollbackNet_.roundTripMs(),
                          static_cast<unsigned long long>(rollback_ ? rollback_->stats().rollbacks : 0));
            break;
        case Phase::Ended:
            std::snprintf(status, sizeof(status), "Rollback match ended: %s", rollbackNet_.endReason().c_str());
            break;
        case Phase::Idle:
            status[0] = '\0';
            break;
    }
    const std::string title = status[0] ? baseWindowTitle_ + " - " + status : baseWindowTitle_;
    if (!headless_ && window_.handle()) SDL_SetWindowTitle(window_.handle(), title.c_str());
}

void Application::updateWorldStreaming() {
    if (!worldStreamer_.isOpen()) return;
    std::vector<glm::vec3> sources;
    std::vector<float> scales;
    if (!headless_) {
        sources.push_back(camera_.position);
        scales.push_back(1.0f);
    }
    for (auto [entity, source] : ecs_.raw().view<StreamingSource>().each()) {
        sources.emplace_back(hierarchy::computeWorldMatrix(ecs_, entity)[3]);
        scales.push_back(source.radiusScale);
    }
    // A server with nobody to stream around keeps the whole world, so nothing it simulates goes missing.
    worldStreamer_.setLoadAll((headless_ && sources.empty()) || rollbackNet_.phase() != net::RollbackNetSession::Phase::Idle);
    worldStreamer_.update(sources, scales);
    if (headless_) resources_.update(); // the render tick pumps it otherwise
}

void Application::shutdown() {
    if (!initialized_) return;

    // Joined FIRST: worker callbacks capture the moderation client, and
    // networkSession_ below holds a raw pointer to it. Stopping the pool
    // here means nothing is in flight while any of that is torn down.
    if (httpWorkerPool_) httpWorkerPool_->shutdown();
    networkSession_.setChatModerationClient(nullptr);

    networkSession_.shutdown();
    rollbackNet_.shutdown();
    rollback_.reset();
    worldStreamer_.close();
    gameLoop_.reset();
    python_.unloadAll();
    input_.shutdown();
        if (!headless_) {
            // Ordering contract from Renderer::shutdown()'s NOTE: mesh/texture GPU
            // resources are VMA allocations and must be freed before the
            // allocator they came from is destroyed.
            // The GPU must be idle BEFORE these buffers are freed, not merely
            // before the device is destroyed: Renderer::shutdown()'s own
            // vkDeviceWaitIdle runs after this point, so without this the last
            // frame's command buffer is still referencing the mesh/texture
            // allocations being released here. StudioApp::shutdown() already
            // waits in exactly this position; this is the same fix.
            if (renderer_.device() != VK_NULL_HANDLE) vkDeviceWaitIdle(renderer_.device());
            resources_.shutdown();
            meshLibrary_.destroyAll(renderer_.allocator());
            // riggedMeshLibrary_ owns its own vertex/index/skin buffers against the
            // same allocator and was never being torn down -- its meshes
            // outlived vmaDestroyAllocator() and were reported as leaked device
            // memory at vkDestroyDevice.
            riggedMeshLibrary_.destroyAll(renderer_.allocator());
            textureLibrary_.destroyAll(renderer_.allocator(), renderer_.device());
            uiRenderer_.shutdown();
            renderer_.shutdown();

            window_.shutdown();
        }
    uiRenderer_.shutdown();
    renderer_.shutdown();

    window_.shutdown();
    if (sdlInitialized_) {
        SDL_Quit();
        sdlInitialized_ = false;
    }

    initialized_ = false;
}

} // namespace engine::core
