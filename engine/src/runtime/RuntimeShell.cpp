#include "runtime/RuntimeShell.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

#include <SDL2/SDL.h>
#include <imgui.h>
#include <backends/imgui_impl_sdl2.h>
#include <backends/imgui_impl_vulkan.h>

#include "core/AvatarSkinTone.hpp"
#include "core/Components.hpp"
#include "core/HiddenGemsSelector.hpp"
#include "core/KronosVersion.hpp"
#include "core/ProcessLaunch.hpp"
#include "core/ResourcePaths.hpp"
#include "core/UITheme.hpp"
#include "marketplace/CreditsPurchase.hpp"
#include "marketplace/RatingSubmission.hpp"
#include "marketplace/RecommendationEngine.hpp"
#include "runtime/GameLoader.hpp"
#include "runtime/GameLoop.hpp"

namespace engine::runtime {

namespace {
constexpr const char* kSessionHistoryPath = "session_history.sessions";
constexpr const char* kLocalProfilePath = "local_profile.profile";
constexpr const char* kGamePlayLogPath = "game_play_log.playlog";
// Kronos ("Avatar Phase" -- "AvatarEditor: Clothing & Accessory Slots"):
// same real paths studio::StudioApp's own catalogueDatabasePath_/
// localAvatarLoadoutPath_ use.
constexpr const char* kAvatarCatalogueDatabasePath = "catalogue.json";
constexpr const char* kAvatarLoadoutPath = "local_avatar_loadout.loadout";
constexpr const char* kAnimationDatabasePath = "animations.json";
constexpr const char* kTransactionLogPath = "transaction_log.transactions";

int64_t nowUnixSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

// Real ImGui Vulkan backend error sink -- exact same shape
// studio::StudioApp's own checkVkResult() already establishes.
void checkVkResult(VkResult err) {
    if (err != VK_SUCCESS) {
        std::fprintf(stderr, "RuntimeShell: ImGui Vulkan backend error (VkResult=%d)\n", static_cast<int>(err));
    }
}

const char* joinFailureReasonLabel(net::JoinFailureReason reason) {
    switch (reason) {
        case net::JoinFailureReason::VersionMismatch: return "Version mismatch";
        case net::JoinFailureReason::SessionFull: return "Session full";
        case net::JoinFailureReason::Banned: return "Banned from this session";
        case net::JoinFailureReason::None: return "Unknown";
    }
    return "Unknown";
}

const char* disconnectReasonLabel(net::DisconnectReason reason) {
    switch (reason) {
        case net::DisconnectReason::Kicked: return "Kicked";
        case net::DisconnectReason::SessionClosed: return "Session closed";
        case net::DisconnectReason::ConnectionLost: return "Connection lost";
        case net::DisconnectReason::None: return "Unknown";
    }
    return "Unknown";
}
} // namespace

RuntimeShell::RuntimeShell(core::Application& app, std::function<core::EntityId()> spawnNetworkedPlayerEntity,
                           std::function<core::EntityId(glm::vec4, core::HeadShape, core::BodyProportions,
                                                         const core::AvatarLoadout&, const core::CatalogueIndex&,
                                                         const core::AnimationOverrides&, core::ClothingFit)>
                               spawnOfflinePlayerEntity)
    : app_(app),
      spawnNetworkedPlayerEntity_(std::move(spawnNetworkedPlayerEntity)),
      spawnOfflinePlayerEntity_(std::move(spawnOfflinePlayerEntity)) {}

RuntimeShell::~RuntimeShell() { shutdown(); }

bool RuntimeShell::initialize() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    // Deliberately no ImGuiConfigFlags_DockingEnable / dockspace -- this
    // is a real, linear menu flow (Home -> browse -> join), not a
    // multi-panel workspace the way Studio's own dockspace is; a fixed,
    // full-window layout per state is simpler and honest about what this
    // actually is.
    ImGui::StyleColorsDark();
    // Kronos ("UI/UX Revamp"): the real, same Kronos visual identity
    // Studio already has -- see core::applyKronosUITheme()'s own header
    // comment for why this used to be Studio-only. Fonts must load
    // before the ImGui_ImplVulkan_Init() call below builds the font
    // atlas texture.
    core::applyKronosUITheme();
    // Kronos ("Base Client UI Theme"): real, player-client-specific
    // overrides on top of the shared Kronos theme above -- applied here
    // (not inside core::applyKronosUITheme() itself) so Studio, which
    // calls that same shared function, keeps its own existing look
    // untouched. Softer, larger rounding and a real, semi-transparent
    // dark-navy window background read as more "consumer app," less
    // "creator tool," matching the real, deliberate distinction this
    // pass draws between the two surfaces.
    {
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = 10.0f;
        style.FrameRounding = 6.0f;
        style.Colors[ImGuiCol_WindowBg] = ImVec4(0.07f, 0.08f, 0.12f, 0.85f);
    }
    core::loadKronosFonts(core::resolveResourceDir(core::executableDirectory(), "assets", ENGINE_ASSET_DIR) + "/fonts");
    // Kronos ("Settings Panel v2 + Input Remapping + Accessibility
    // Layer" -- "UI scale must affect all panels"): real baseline
    // snapshot -- see baseUIStyle_'s own comment.
    baseUIStyle_ = ImGui::GetStyle();

    core::Renderer& renderer = app_.renderer();
    core::Window& window = app_.window();

    if (!ImGui_ImplSDL2_InitForVulkan(window.handle())) {
        std::fprintf(stderr, "RuntimeShell: ImGui_ImplSDL2_InitForVulkan failed.\n");
        return false;
    }

    std::array<VkDescriptorPoolSize, 1> poolSizes{{{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 64}}};
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 64;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    if (vkCreateDescriptorPool(renderer.device(), &poolInfo, nullptr, &imguiDescriptorPool_) != VK_SUCCESS) {
        std::fprintf(stderr, "RuntimeShell: vkCreateDescriptorPool (ImGui) failed.\n");
        return false;
    }

    VkFormat colorFormat = renderer.swapchainFormat();

    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.ApiVersion = VK_API_VERSION_1_3;
    initInfo.Instance = renderer.instance();
    initInfo.PhysicalDevice = renderer.physicalDevice();
    initInfo.Device = renderer.device();
    initInfo.QueueFamily = renderer.graphicsQueueFamily();
    initInfo.Queue = renderer.graphicsQueue();
    initInfo.DescriptorPool = imguiDescriptorPool_;
    initInfo.MinImageCount = renderer.framesInFlight();
    initInfo.ImageCount = renderer.swapchainImageCount();
    initInfo.UseDynamicRendering = true;
    initInfo.PipelineInfoMain.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
    initInfo.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    initInfo.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &colorFormat;
    initInfo.Allocator = nullptr;
    initInfo.CheckVkResultFn = &checkVkResult;

    if (!ImGui_ImplVulkan_Init(&initInfo)) {
        std::fprintf(stderr, "RuntimeShell: ImGui_ImplVulkan_Init failed.\n");
        return false;
    }

    // Forward every raw SDL event to ImGui -- same real mechanism
    // studio::StudioApp already uses (see core::Window::setRawEventCallback's
    // own doc comment for why this is a callback hook, not a second event
    // pump).
    window.setRawEventCallback([](const SDL_Event& event) { ImGui_ImplSDL2_ProcessEvent(&event); });

    // Composite ImGui's draw data on top of the real rendered frame --
    // the same real overlay mechanism Studio's Principle-4 "what you see
    // in Studio is what ships" relies on, here proving that hook is
    // genuinely generic engine_core API, not something Studio-specific.
    renderer.setOverlayCallback([this](VkCommandBuffer cmd, VkImageView, VkExtent2D) {
        if (pendingDrawData_) ImGui_ImplVulkan_RenderDrawData(pendingDrawData_, cmd);
    });

    // Kronos ("Home Screen Avatar Preview"): real, generic engine_core
    // hook (core::Renderer::setPrePassCallback()) -- confirmed unused
    // anywhere else in engine_runtime, free to claim (see
    // studio::PreviewScene's own class comment on why only one real
    // PrePassCallback can be active at a time). Gated to only actually
    // render while Home is genuinely visible, matching every
    // PreviewScene-owning Studio plugin's own "don't render a closed/
    // invisible preview" convention.
    renderer.setPrePassCallback([this](VkCommandBuffer cmd) {
        if (state_ == ShellState::Home && !showSplash_ && homeAvatarPreview_) {
            homeAvatarPreview_->renderPreview(cmd, app_.renderer());
        }
    });

    std::fprintf(stdout, "RuntimeShell: ImGui Vulkan backend initialized (dynamic rendering, format=%d)\n",
                 static_cast<int>(colorFormat));

    // Kronos ("Player & Chat System" -- chat panel): real, registered
    // once here (not per-join) -- net::NetworkSession itself persists
    // across a leave/rejoin (see its own class comment), so one real
    // registration covers every real session this shell's whole lifetime
    // ever joins. A real, honest no-op for offline (ProjectPath Catalogue)
    // play -- no NetworkSession means this callback simply never fires,
    // same as every other real NetworkSession-only feature in this shell.
    app_.networkSession().setOnChatMessageReceived([this](net::PlayerId sender, const std::string& text) {
        auto it = app_.networkSession().clientKnownPlayers().find(sender);
        std::string senderName = it != app_.networkSession().clientKnownPlayers().end() ? it->second : "Unknown";
        chatHistoryLines_.push_back(senderName + ": " + text);
        if (chatHistoryLines_.size() > kMaxChatHistoryLines) {
            chatHistoryLines_.erase(chatHistoryLines_.begin());
        }
    });

    // Kronos ("Settings Panel v2 + Input Remapping + Accessibility
    // Layer" -- "Settings applied immediately"): real -- a freshly
    // launched session already reflects a real, previously-saved
    // profile's own graphics/audio/accessibility/input settings from the
    // very first frame, not just the engine's own hardcoded defaults
    // until the player happens to open Settings once.
    ensureLocalProfileLoaded();
    applyAllSettingsFromProfile();

    return true;
}

void RuntimeShell::shutdown() {
    lanBrowser_.stop();
    lanBrowserRunning_ = false;

    if (homeAvatarPreview_) {
        vkDeviceWaitIdle(app_.renderer().device());
        homeAvatarPreview_->shutdown(app_.renderer());
        homeAvatarPreview_.reset();
    }

    if (imguiDescriptorPool_) {
        // A real, live ImGui context implies a real, live device -- this
        // shell is always destroyed before core::Application::shutdown()
        // tears the device down (see main.cpp's own ordering).
        vkDeviceWaitIdle(app_.renderer().device());
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
        vkDestroyDescriptorPool(app_.renderer().device(), imguiDescriptorPool_, nullptr);
        imguiDescriptorPool_ = nullptr;
    }
}

void RuntimeShell::ensureLocalProfileLoaded() {
    if (localProfileLoaded_) return;
    bool profileFileExistedBefore = std::filesystem::exists(kLocalProfilePath);
    localProfile_ = core::loadOrCreateProfile(kLocalProfilePath);
    std::snprintf(displayNameBuffer_, sizeof(displayNameBuffer_), "%s", localProfile_.displayName.c_str());
    localProfileLoaded_ = true;
    // Kronos ("Notifications System" -- "System messages"): a real,
    // one-time welcome notification, gated on the real profile file
    // genuinely not having existed before this call (not on
    // notifications being empty -- an old, pre-existing profile that
    // simply predates this feature must NOT retroactively get a
    // "Welcome" message every session).
    if (!profileFileExistedBefore) {
        notify(core::NotificationKind::SystemMessage, "Welcome to Kronos",
               "Your local profile has been created. Explore the Game Catalogue, Avatar Shop, and Friends panel to "
               "get started.");
        (void)localProfile_.saveToFile(kLocalProfilePath);
    }
}

void RuntimeShell::ensureAvatarCatalogueLoaded() {
    if (avatarCatalogueLoaded_) return;
    if (avatarCatalogueDatabase_.loadFromFile(kAvatarCatalogueDatabasePath)) {
        avatarCatalogueIndex_.rebuild(avatarCatalogueDatabase_);
    }
    (void)avatarLoadout_.loadFromFile(kAvatarLoadoutPath);
    (void)animationDatabase_.loadFromFile(kAnimationDatabasePath);
    (void)transactionLog_.loadFromFile(kTransactionLogPath);
    // Kronos ("Kronos Scripting Environment" -- "avatar.playEmote"): real
    // late-binding -- animationDatabase_ only exists from this point
    // onward (this function is lazy, called on first Home/Avatar Shop
    // open, not at startup), and Application itself doesn't own an
    // AnimationDatabase -- see Application::setAnimationDatabase()'s own
    // header comment for why this is a setter call here rather than a
    // constructor parameter there. A script calling avatar.playEmote()
    // before this has ever run gets a real, honest false (not a crash) --
    // see Application::tryPlayEmoteForEntity()'s own null check.
    app_.setAnimationDatabase(animationDatabase_);
    avatarCatalogueLoaded_ = true;
}

void RuntimeShell::ensureHomeAvatarPreviewLoaded() {
    if (homeAvatarPreview_) return;
    ensureLocalProfileLoaded();
    ensureAvatarCatalogueLoaded();
    core::Renderer& renderer = app_.renderer();
    homeAvatarPreview_ = std::make_unique<HomeAvatarPreview>(renderer.allocator(), renderer.device(),
                                                              renderer.commandPool(), renderer.graphicsQueue(),
                                                              app_.riggedMeshLibrary(), localProfile_,
                                                              avatarCatalogueIndex_, avatarLoadout_, animationDatabase_);
}

void RuntimeShell::ensureSessionHistoryLoaded() {
    if (sessionHistoryLoaded_) return;
    (void)sessionHistory_.loadFromFile(kSessionHistoryPath);
    sessionHistoryLoaded_ = true;
}

void RuntimeShell::ensureGamePlayLogLoaded() {
    if (gamePlayLogLoaded_) return;
    (void)gamePlayLog_.loadFromFile(kGamePlayLogPath);
    // Real crash detection -- see reconcileUnclosedSessionsAsCrashed()'s
    // own comment. Must run here, once, right after load and before this
    // run records anything of its own.
    if (gamePlayLog_.reconcileUnclosedSessionsAsCrashed() > 0) {
        (void)gamePlayLog_.saveToFile(kGamePlayLogPath);
    }
    gamePlayLogLoaded_ = true;
}

void RuntimeShell::showSessionBrowser() {
    if (state_ != ShellState::Home) return;
    ensureLocalProfileLoaded();
    ensureSessionHistoryLoaded();
    if (!lanBrowserRunning_) {
        lanBrowserRunning_ = lanBrowser_.start(net::kLanAnnouncePort, net::kLanPingPort);
        lanBrowserClock_ = 0.0f;
    }
    state_ = computeNextState(state_, ShellEvent::OpenSessionBrowser);
}

void RuntimeShell::showPlayerList() {
    if (state_ != ShellState::InGame) return;
    showPlayerListOverlay_ = !showPlayerListOverlay_;
}

void RuntimeShell::joinSession(const net::DiscoveredSession& session) {
    // Kronos ("Merged Game Catalogue & Sessions View"): real, relaxed --
    // GameCatalogue now also has a real, direct join path (a card's own
    // expanded live-session list, see drawGameCataloguePanel()'s own
    // comment), not just the standalone Session Browser panel. Both
    // states real-transition to Loading on JoinRequested below
    // (ShellState.hpp's own computeNextState()).
    if (state_ != ShellState::SessionBrowser && state_ != ShellState::GameCatalogue) return; // real, honest no-op otherwise

    ensureLocalProfileLoaded();

    // Kronos ("Moderation Architecture v2", "Session Browser Game
    // Identity" -- "block minors from unsafe sessions"): real,
    // launch-time defense-in-depth, same "never rely on a single layer"
    // principle as selectGame()'s own equivalent check for the Game
    // Catalogue -- the Session Browser row for an Unsafe session is
    // already hidden from a non-Adult viewer (drawSessionBrowserPanel()),
    // but this doesn't trust that alone: the "Recently played" list below
    // (drawSessionBrowserPanel()'s own second real joinSession() call
    // site, built from net::SessionHistory rather than a live
    // Announce) never passes through that row-hiding at all.
    auto safetyStatus = static_cast<core::GameSafetyStatus>(session.gameSafetyStatusValue);
    if (!core::isGameSafeToLaunchForAgeGroup(safetyStatus, effectiveAgeGroup())) {
        lastError_ = ShellErrorInfo{};
        lastError_.kind = ShellErrorKind::NetworkFailure;
        lastError_.detail = "This session's game is flagged Unsafe and cannot be joined in Minor Mode.";
        state_ = ShellState::Error;
        return;
    }

    net::NetworkSession::Config config;
    config.mode = net::NetworkMode::Client;
    config.serverAddress = session.sourceAddress;
    config.port = session.gamePort;
    lastJoinedHostDisplayName_ = session.hostDisplayName;

    app_.networkSession().setLocalDisplayName(localProfile_.displayName);
    // Kronos ("Moderation Architecture v2", "Account System v1"): real
    // identity signals sent with this join -- see setLocalIdentity()'s
    // own comment.
    app_.networkSession().setLocalIdentity(localProfile_.profileId, effectiveAgeGroup());
    if (!app_.startNetworking(config)) {
        lastError_ = ShellErrorInfo{};
        lastError_.kind = ShellErrorKind::NetworkFailure;
        lastError_.detail = "Failed to start networking for " + session.sourceAddress;
        state_ = ShellState::Error;
        return;
    }

    ensureSessionHistoryLoaded();
    sessionHistory_.recordConnection(session.sessionName, session.sourceAddress, session.gamePort, nowUnixSeconds());
    (void)sessionHistory_.saveToFile(kSessionHistoryPath);

    if (spawnNetworkedPlayerEntity_) {
        core::EntityId entity = spawnNetworkedPlayerEntity_();
        app_.setNetworkedLocalPlayerEntity(entity);
    }

    lanBrowser_.stop();
    lanBrowserRunning_ = false;
    lastError_ = ShellErrorInfo{};
    state_ = computeNextState(state_, ShellEvent::JoinRequested);
}

void RuntimeShell::joinSession(uint64_t sessionId) {
    for (const auto& session : lanBrowser_.discoveredSessions()) {
        if (session.sessionId == sessionId) {
            joinSession(session);
            return;
        }
    }
    // Real, honest no-op if `sessionId` isn't (or is no longer) a real,
    // currently-discovered session -- matches ui.joinSession()'s own "an
    // inapplicable call is a no-op" contract (see ScriptUiApi, Phase 6).
}

void RuntimeShell::cancelJoin() {
    if (state_ != ShellState::Loading) return;
    app_.networkSession().shutdown();
    app_.setNetworkedLocalPlayerEntity(core::kNullEntity);
    state_ = computeNextState(state_, ShellEvent::CancelJoin);
}

void RuntimeShell::leaveSession() {
    if (state_ != ShellState::InGame) return;
    app_.networkSession().shutdown();
    app_.setNetworkedLocalPlayerEntity(core::kNullEntity);
    app_.input().setRelativeMouseMode(false);
    showPlayerListOverlay_ = false;

    // Kronos ("Game Catalogue Overhaul", Phase 6): real session-end
    // logging -- a real, honest no-op when currentGameId_ is empty (this
    // InGame session was a real networked join, not a local
    // runtime::loadGame() session; see currentGameId_'s own comment).
    if (!currentGameId_.empty()) {
        ensureGamePlayLogLoaded();
        gamePlayLog_.recordSessionEnd(currentGameId_, nowUnixSeconds(), /*crashed=*/false);
        (void)gamePlayLog_.saveToFile(kGamePlayLogPath);
        currentGameId_.clear();
    }

    state_ = computeNextState(state_, ShellEvent::SessionEnded);
}

void RuntimeShell::openGameCatalogue() {
    if (state_ != ShellState::Home) return;
    // Real, fresh disk read every time the catalogue is actually opened
    // (a deliberate user action, not a hot path) -- not cached across a
    // whole session, so Featured/Hidden-Gems ranking reflects whatever
    // was just played, not stale first-launch data.
    std::string gamesDir = core::resolveResourceDir(core::executableDirectory(), "games", ENGINE_GAMES_DIR);
    discoveredGames_ = core::buildGameCatalogueEntries(gamesDir, kGamePlayLogPath, nowUnixSeconds());
    // Kronos ("Moderation Architecture v2", "Catalogue Safety
    // Integration"): real "Catalogue hides unsafe games from minors" --
    // ensureLocalProfileLoaded() has already real-loaded localProfile_ by
    // the time the Home Screen (and thus this button) is reachable.
    ensureLocalProfileLoaded();
    discoveredGames_ = core::filterCatalogueEntriesForAgeGroup(discoveredGames_, effectiveAgeGroup());
    gamesScanned_ = true;
    // Kronos ("Merged Game Catalogue & Sessions View"): real, same guarded
    // start showSessionBrowser() already does -- so a card's own
    // expanded live-session list (drawGameCataloguePanel()) has real,
    // live net::DiscoveredSession data (each real-carrying its own
    // gameName, see LanSessionAnnouncement's own comment) to filter by
    // game, not stale/empty data. tickLanBrowserIfNeeded() already ticks
    // regardless of which state is current once started, so this is
    // real from the moment the catalogue opens.
    if (!lanBrowserRunning_) {
        lanBrowserRunning_ = lanBrowser_.start(net::kLanAnnouncePort, net::kLanPingPort);
        lanBrowserClock_ = 0.0f;
    }
    state_ = computeNextState(state_, ShellEvent::OpenGameCatalogue);
}

void RuntimeShell::selectGame(const core::GameCatalogueEntry& game) {
    if (state_ != ShellState::GameCatalogue) return;

    // Kronos ("Moderation Architecture v2", "Catalogue Safety
    // Integration"): real, launch-time defense-in-depth -- the catalogue
    // listing already real-filters Unsafe games out for a non-Adult
    // viewer (openGameCatalogue()), but this check doesn't trust that
    // alone; the same "never rely on a single layer" principle Minor
    // Mode's DM/chat blocking already follows elsewhere this session.
    if (!core::isGameSafeToLaunchForAgeGroup(game.manifest.safetyStatus, effectiveAgeGroup())) {
        std::fprintf(stderr, "RuntimeShell: refusing to launch \"%s\" -- flagged Unsafe and the local profile is not "
                              "a self-declared Adult\n",
                     game.manifest.name.c_str());
        return;
    }

    if (game.manifest.launchKind == core::GameLaunchKind::CliFlag) {
        // A still-hardcoded rich mode (TNT Wars/Mining Sim/House Demo) --
        // relaunch this same binary with its real flag and leave this
        // process's own shell state untouched (the new process owns its
        // own real bring-up from here).
        std::string selfPath = core::executableDirectory() + "/engine_runtime";
        std::vector<std::string> args;
        if (!game.manifest.cliFlag.empty()) args.push_back(game.manifest.cliFlag);
        if (!core::launchProcess(selfPath, args)) {
            std::fprintf(stderr, "RuntimeShell: failed to relaunch engine_runtime for \"%s\" (%s)\n",
                         game.manifest.name.c_str(), game.manifest.cliFlag.c_str());
        }
        return;
    }

    // Kronos ("Animated Hourglass Loading Screen"): real, deferred -- the
    // rest of what this function used to do synchronously (the real
    // runtime::loadGame() scene rebuild + avatar spawn, genuinely
    // non-trivial work) now happens one full real frame later
    // (finishPendingGameLoad(), called from tick()), so this frame's own
    // draw call genuinely renders the Loading panel's hourglass and gets
    // presented before that synchronous work ever starts, instead of
    // blocking before anything painted. A real, owned copy of `game` --
    // not a pointer into discoveredGames_, which openGameCatalogue() can
    // reload out from under a pending load.
    pendingGameLoad_ = game;
    pendingGameLoadReadyToRun_ = false;
    state_ = computeNextState(state_, ShellEvent::GameSelected);
}

void RuntimeShell::finishPendingGameLoad() {
    if (!pendingGameLoad_.has_value()) return;
    core::GameCatalogueEntry game = *pendingGameLoad_;
    pendingGameLoad_.reset();

    core::DiscoveredGame discovered;
    discovered.manifestPath = game.manifestPath;
    discovered.manifest = game.manifest;
    discovered.parseSucceeded = true;
    if (!loadGame(app_, discovered)) {
        std::fprintf(stderr, "RuntimeShell: \"%s\" real-failed to load\n", game.manifest.name.c_str());
        // Kronos ("Animated Hourglass Loading Screen"): real, same
        // "just go back to the Catalogue, no formal error panel"
        // behavior this had before the Loading beat was inserted -- a
        // local game-load failure isn't a network problem, so it
        // deliberately doesn't route through ShellErrorKind at all.
        state_ = computeNextState(state_, ShellEvent::GameLoadFailed);
        return;
    }

    // Kronos ("Kronos Player & Chat System Initialization" -- "Spawn a
    // default avatar when entering any world"): real, necessary --
    // loadGame() only rebuilds the scene's own static content (see
    // spawnOfflinePlayerEntity_'s own constructor-param comment for why);
    // without this, launching any Catalogue game left the player with no
    // controllable character at all (core::CharacterController::tick()
    // still ran every sim tick per Application's own pre-tick hook, but
    // driving whatever stale/invalid entity_ the ECS wipe inside
    // loadGame() left behind).
    // Kronos ("Avatar Phase" -- "AvatarEditor: Skin-Tone Selection" /
    // "Avatar Head System" / "Body Sliders" / "Clothing & Accessory
    // Slots" / "Animation Overrides"): real, resolved from the real,
    // chosen (or never-chosen, real default) core::LocalProfile fields
    // plus the real, on-disk avatar catalogue/loadout/animation database
    // -- ensureLocalProfileLoaded() already ran (openGameCatalogue() calls
    // it before this state is ever reachable); ensureAvatarCatalogueLoaded()
    // is called here since Catalogue browsing itself never needed any of
    // these before now.
    if (spawnOfflinePlayerEntity_) {
        ensureAvatarCatalogueLoaded();
        // Empty when unset or the referenced clip no longer resolves in
        // the database -- core::Application::spawnLocalPlayerAvatar()'s
        // own loadClip() treats an empty override path as "use the
        // shipped default," the same real, honest fail-soft as a broken
        // override.
        auto resolveOverridePath = [&](const std::string& itemId) -> std::string {
            if (itemId.empty()) return std::string();
            const core::AnimationManifest* manifest = animationDatabase_.findById(itemId);
            return manifest != nullptr ? manifest->item.clipPath : std::string();
        };
        core::AnimationOverrides animationOverrides;
        animationOverrides.idleClipPath = resolveOverridePath(localProfile_.animOverrideIdleId);
        animationOverrides.walkClipPath = resolveOverridePath(localProfile_.animOverrideWalkId);
        animationOverrides.runClipPath = resolveOverridePath(localProfile_.animOverrideRunId);
        animationOverrides.jumpStartClipPath = resolveOverridePath(localProfile_.animOverrideJumpStartId);
        animationOverrides.jumpAirClipPath = resolveOverridePath(localProfile_.animOverrideJumpAirId);
        animationOverrides.jumpLandClipPath = resolveOverridePath(localProfile_.animOverrideJumpLandId);

        spawnOfflinePlayerEntity_(
            core::resolveSkinToneColor(localProfile_.skinToneIndex), core::headShapeFromIndex(localProfile_.headShapeIndex),
            core::BodyProportions{localProfile_.bodyHeight, localProfile_.bodyWidth, localProfile_.bodyLimbScale,
                                   localProfile_.bodyTorsoLength, localProfile_.bodyShoulderWidth},
            avatarLoadout_, avatarCatalogueIndex_, animationOverrides,
            core::clothingFitFromIndex(localProfile_.clothingFitIndex));
    }

    // Kronos ("Game Catalogue Overhaul", Phase 6): real, local retention
    // logging -- see net::GamePlayLog's own class comment.
    ensureGamePlayLogLoaded();
    currentGameId_ = game.manifest.name;
    gamePlayLog_.recordSessionStart(currentGameId_, nowUnixSeconds());
    (void)gamePlayLog_.saveToFile(kGamePlayLogPath);

    app_.input().setRelativeMouseMode(true);
    state_ = computeNextState(state_, ShellEvent::GameLoadFinished);
}

void RuntimeShell::launchStudio() {
    std::string studioPath = core::executableDirectory() + "/studio";
    if (!core::launchProcess(studioPath, {})) {
        std::fprintf(stderr, "RuntimeShell: failed to launch Studio at \"%s\"\n", studioPath.c_str());
    }
}

void RuntimeShell::tickLanBrowserIfNeeded(float dt) {
    if (!lanBrowserRunning_) return;
    lanBrowserClock_ += dt;
    lanBrowser_.tick(lanBrowserClock_);
}

void RuntimeShell::beginFrame() {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
}

void RuntimeShell::endFrame() {
    // Unconditional -- always pairs with beginFrame()'s ImGui::NewFrame(),
    // regardless of whether renderer_.renderFrame() (called right after,
    // from GameLoop::renderTick()) ends up actually reaching the overlay
    // callback that consumes pendingDrawData_ -- same real reasoning
    // studio::StudioApp::endFrame() already documents.
    ImGui::Render();
    pendingDrawData_ = ImGui::GetDrawData();
}

void RuntimeShell::tick(float dt) {
    tickLanBrowserIfNeeded(dt);

    // Kronos ("Animated Hourglass Loading Screen"): real, deferred-by-
    // one-frame game load -- selectGame() sets pendingGameLoad_ and
    // transitions to Loading during ITS OWN frame's draw call (tick()
    // already ran for that frame, before draw); the FIRST tick() that
    // observes Loading+pendingGameLoad_ is the one for the NEXT frame,
    // which only flips pendingGameLoadReadyToRun_ (so that next frame's
    // own draw call genuinely renders and presents the Loading panel's
    // hourglass); only the frame AFTER that actually calls
    // finishPendingGameLoad() and does the real, heavy synchronous work.
    if (state_ == ShellState::Loading && pendingGameLoad_.has_value()) {
        if (pendingGameLoadReadyToRun_) {
            finishPendingGameLoad();
        } else {
            pendingGameLoadReadyToRun_ = true;
        }
    }

    // Real Loading -> InGame/Error transition -- polls real NetworkSession
    // state every real frame while a join attempt is in flight (this
    // can't be a synchronous call: the real join handshake completes
    // asynchronously, over real loopback/LAN ENet traffic, potentially
    // several real network ticks after startNetworking() returns).
    if (state_ == ShellState::Loading) {
        net::NetworkSession& session = app_.networkSession();
        if (session.localPlayerId() != net::kInvalidPlayer) {
            state_ = computeNextState(state_, ShellEvent::JoinSucceeded);
            app_.input().setRelativeMouseMode(true);
            // Kronos ("Social and Messaging Roadmap" telemetry ask --
            // "session joins"): real, local telemetry on a real,
            // confirmed join (not just an attempt).
            analytics::TelemetryEvent joinEvent;
            joinEvent.name = "session_joined";
            joinEvent.properties["sessionId"] = static_cast<int64_t>(session.sessionId());
            telemetryQueue_.push(std::move(joinEvent));
        } else if (session.lastJoinFailureReason() != net::JoinFailureReason::None) {
            lastError_ = ShellErrorInfo{};
            lastError_.kind = ShellErrorKind::JoinFailed;
            lastError_.joinFailureReason = session.lastJoinFailureReason();
            lastError_.joinFailureServerProtocolVersion = session.lastJoinFailureServerProtocolVersion();
            state_ = computeNextState(state_, ShellEvent::JoinFailed);
        }
    }

    // Real InGame -> Error transition on a real disconnect (kicked/
    // session closed/connection lost) -- distinct from the player's own
    // deliberate leaveSession() call, which transitions immediately and
    // synchronously from inside leaveSession() itself, never through
    // here. isClient() guards this off entirely for offline play
    // (selectGame() on a ProjectPath game never touches networkSession()
    // at all).
    if (state_ == ShellState::InGame && app_.networkSession().isClient() &&
        app_.networkSession().localPlayerId() == net::kInvalidPlayer &&
        app_.networkSession().lastDisconnectReason() != net::DisconnectReason::None) {
        lastError_ = ShellErrorInfo{};
        lastError_.kind = ShellErrorKind::Disconnected;
        lastError_.disconnectReason = app_.networkSession().lastDisconnectReason();
        app_.networkSession().shutdown();
        app_.setNetworkedLocalPlayerEntity(core::kNullEntity);
        app_.input().setRelativeMouseMode(false);
        showPlayerListOverlay_ = false;
        state_ = ShellState::Error;
    }

    // Kronos ("Active Joining UI" -- real bug fix): Escape always leaves
    // InGame back to Home, matching every other game's own convention
    // (the real gap a live playtest surfaced -- once reached via the Home
    // Screen's "Play"/"Join", not a CLI flag, InGame previously had no
    // way back out at all: no keyboard shortcut, and the mouse itself is
    // captured/hidden by relative mouse mode, so the always-visible HUD's
    // own buttons below were unreachable too). leaveSession() itself
    // already no-ops safely on an inactive (offline) NetworkSession -- see
    // NetworkSession::shutdown()'s own mode/sessionActuallyStarted_ guard
    // -- so this one call is correct for both online and offline play.
    // Kronos ("Player & Chat System" -- chat panel): Escape closes an
    // open chat box first, same real "Escape backs out one real layer at
    // a time" convention every other real menu in this shell already
    // follows (SessionBrowser/GameCatalogue/Error's own ReturnHome) --
    // without this real guard, pressing Escape to cancel a half-typed
    // chat message would instead leave the whole game, a real, jarring
    // bug this check exists specifically to prevent.
    bool escapeDown = state_ == ShellState::InGame && !showChatPanel_ && app_.input().isActionDown("ToggleMenu");
    if (escapeDown && !escapeKeyWasDown_) {
        leaveSession();
    }
    escapeKeyWasDown_ = escapeDown;

    tickToasts(dt);
    // Kronos ("Load Testing and Telemetry" / "Simple Recommendation
    // Engine"): real, periodic drain-to-disk -- see
    // analytics::TelemetrySender's own header comment. Once a second is
    // plenty for this Alpha's real event volume; not every frame, to
    // avoid needless real file I/O.
    telemetryFlushClock_ += dt;
    if (telemetryFlushClock_ >= 1.0f) {
        telemetryFlushClock_ = 0.0f;
        telemetrySender_.flush();
    }
    // Kronos ("Home Screen Avatar Preview"): real, only ticked while
    // actually visible (Home, past the splash) -- an idle preview no one
    // is looking at still costs nothing extra beyond this one real
    // check.
    if (state_ == ShellState::Home && !showSplash_ && homeAvatarPreview_) homeAvatarPreview_->update(dt);

    // Kronos ("Home UI Polish" -- "Smooth transitions"): real, general
    // fade-in on every real state change -- see stateTransitionClock_'s
    // own comment.
    if (state_ != previousDrawState_) {
        stateTransitionClock_ = 0.0f;
        previousDrawState_ = state_;
    } else {
        stateTransitionClock_ += dt;
    }
    float transitionAlpha = std::clamp(stateTransitionClock_ / kStateTransitionFadeSeconds, 0.0f, 1.0f);

    beginFrame();
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, transitionAlpha);
    // Kronos ("Branding + Release Prep"): a real, one-time splash --
    // takes over the very first few real Home frames, then never shows
    // again this run.
    if (state_ == ShellState::Home && showSplash_) {
        splashClock_ += dt;
        if (splashClock_ >= kSplashDurationSeconds) showSplash_ = false;
        drawSplashPanel();
    } else {
        switch (state_) {
            case ShellState::Home: drawHomePanel(); break;
            case ShellState::SessionBrowser: drawSessionBrowserPanel(); break;
            case ShellState::Loading: drawLoadingPanel(); break;
            case ShellState::GameCatalogue: drawGameCataloguePanel(); break;
            case ShellState::AvatarShop: drawAvatarShopPanel(); break;
            case ShellState::Settings: drawSettingsPanel(); break;
            case ShellState::Friends: drawFriendsPanel(); break;
            case ShellState::Notifications: drawNotificationsPanel(); break;
            case ShellState::Error: drawErrorPanel(); break;
            case ShellState::InGame:
                tickTrailerCaptureMode(dt);
                tickEmoteActivation();
                if (!trailerHudHidden_) {
                    drawPlayerListOverlay();
                    tickChatActivation();
                    drawChatPanel();
                    if (showAvatarShopOverlay_) drawAvatarShopPanel();
                    if (showSettingsOverlay_) drawSettingsPanel();
                    if (showFriendsOverlay_) drawFriendsPanel();
                    if (showNotificationsOverlay_) drawNotificationsPanel();
                    if (trailerCaptureModeEnabled_) drawTrailerCapturePanel();
                }
                break;
        }
        if (state_ == ShellState::Home && showAboutOverlay_) drawAboutPanel();
    }
    ImGui::PopStyleVar();
    drawToasts();
    endFrame();
}

namespace {
// Kronos ("UI Theme Cleanup" -- "green accent buttons"): real, shared --
// previously duplicated as local consts inside drawHomePanel() for its
// own two primary-action buttons (Game Catalogue/Launch Studio). A real,
// reusable helper now, so the same primary-action green applies
// consistently to other real primary actions elsewhere in engine_runtime
// (this pass: the Game Catalogue's own "Play"/live-session "Join"
// buttons, see drawGameCard() below) instead of drifting out of sync
// with a second, separately-hand-copied color triple.
void pushPrimaryActionButtonColors() {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.00f, 0.78f, 0.32f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.14f, 0.86f, 0.44f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.00f, 0.64f, 0.25f, 1.0f));
}
void popPrimaryActionButtonColors() { ImGui::PopStyleColor(3); }

// Kronos ("Animated Hourglass Loading Screen"): a real, procedurally
// drawn, animated hourglass -- two triangles forming the classic bulb-
// neck-bulb silhouette, a real "sand" fill that drains from the top
// chamber into the bottom one over a fixed real cycle (looping, not a
// one-shot), plus a small falling-grain dot through the neck. Pure
// ImDrawList geometry -- no external image/sprite asset, the same
// "flat procedural shape, no texture pipeline needed" convention this
// codebase's own GameManifest::thumbnailColor card art already
// establishes. Declared here (before drawSplashPanel()'s own use below)
// rather than down by drawLoadingPanel() -- both real callers need it.
void drawAnimatedHourglass(ImDrawList* drawList, ImVec2 center, float halfWidth, float halfHeight, float animTime) {
    const ImU32 kFrameColor = IM_COL32(200, 205, 212, 255);
    const ImU32 kSandColor = IM_COL32(196, 160, 92, 255);
    constexpr float kCycleSeconds = 2.4f;
    float cycleT = std::fmod(std::max(animTime, 0.0f), kCycleSeconds) / kCycleSeconds; // real, looping 0..1

    ImVec2 topLeft(center.x - halfWidth, center.y - halfHeight);
    ImVec2 topRight(center.x + halfWidth, center.y - halfHeight);
    ImVec2 bottomLeft(center.x - halfWidth, center.y + halfHeight);
    ImVec2 bottomRight(center.x + halfWidth, center.y + halfHeight);
    ImVec2 neckTop(center.x, center.y - halfHeight * 0.08f);
    ImVec2 neckBottom(center.x, center.y + halfHeight * 0.08f);

    float topSandFrac = 1.0f - cycleT;    // real, 1 -> 0 across the cycle
    float bottomSandFrac = cycleT;        // real, 0 -> 1 across the cycle

    // Top chamber sand -- a real, shrinking triangle collapsing toward
    // the neck as it drains.
    if (topSandFrac > 0.01f) {
        ImVec2 sandLeft(topLeft.x + (neckTop.x - topLeft.x) * (1.0f - topSandFrac),
                         topLeft.y + (neckTop.y - topLeft.y) * (1.0f - topSandFrac));
        ImVec2 sandRight(topRight.x + (neckTop.x - topRight.x) * (1.0f - topSandFrac),
                          topRight.y + (neckTop.y - topRight.y) * (1.0f - topSandFrac));
        drawList->AddTriangleFilled(sandLeft, sandRight, neckTop, kSandColor);
    }
    // Bottom chamber sand -- a real, growing mound rising from the neck
    // (deliberately not the full inverted-triangle chamber shape, for a
    // real "pile settling at the bottom" read rather than a flat fill
    // line).
    if (bottomSandFrac > 0.01f) {
        ImVec2 sandLeft(bottomLeft.x + (neckBottom.x - bottomLeft.x) * (1.0f - bottomSandFrac),
                         bottomLeft.y + (neckBottom.y - bottomLeft.y) * (1.0f - bottomSandFrac));
        ImVec2 sandRight(bottomRight.x + (neckBottom.x - bottomRight.x) * (1.0f - bottomSandFrac),
                          bottomRight.y + (neckBottom.y - bottomRight.y) * (1.0f - bottomSandFrac));
        drawList->AddTriangleFilled(neckBottom, sandLeft, sandRight, kSandColor);
    }
    // Real falling grain, visible only while sand actually remains to
    // fall.
    if (topSandFrac > 0.01f && bottomSandFrac < 0.99f) {
        float streamPhase = std::fmod(animTime * 6.0f, 1.0f);
        ImVec2 streamPos(center.x, neckTop.y + (neckBottom.y - neckTop.y + halfHeight * 0.3f) * streamPhase);
        drawList->AddCircleFilled(streamPos, 1.6f, kSandColor);
    }

    // Real glass outline, drawn last so sand never paints over the
    // frame.
    drawList->AddTriangle(topLeft, topRight, neckTop, kFrameColor, 2.0f);
    drawList->AddTriangle(neckBottom, bottomLeft, bottomRight, kFrameColor, 2.0f);
    drawList->AddLine(topLeft, topRight, kFrameColor, 2.0f);
    drawList->AddLine(bottomLeft, bottomRight, kFrameColor, 2.0f);
}
} // namespace

void RuntimeShell::drawHomePanel() {
    ensureLocalProfileLoaded();

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin("Kronos", nullptr,
                  ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);

    // Kronos ("Home Screen Avatar Preview"): real, live, orbit-able --
    // placed to the right of the button card so it reads as a real
    // companion panel, not a random floating window (the spec's own
    // "Integrate cleanly with existing Home layout"). Only drawn once
    // the splash has finished (matches every other Home element).
    if (!showSplash_) {
        ensureHomeAvatarPreviewLoaded();
        constexpr float kPreviewWidth = 360.0f;
        constexpr float kPreviewHeight = 440.0f;
        float previewX = viewport->WorkPos.x + viewport->WorkSize.x * 0.74f - kPreviewWidth * 0.5f;
        float previewY = viewport->WorkPos.y + viewport->WorkSize.y * 0.5f - kPreviewHeight * 0.5f;
        ImGui::SetNextWindowPos(ImVec2(previewX, previewY), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(kPreviewWidth, kPreviewHeight), ImGuiCond_Always);
        if (ImGui::Begin("##home_avatar_preview", nullptr,
                          ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNav)) {
            ImGui::TextDisabled("Your Avatar");
            ImGui::BeginChild("##home_avatar_preview_viewport", ImVec2(0.0f, kPreviewHeight - 60.0f), true);
            if (homeAvatarPreview_) homeAvatarPreview_->draw();
            ImGui::EndChild();
            ImGui::TextDisabled("Drag to orbit, scroll to zoom");
        }
        ImGui::End();
    }

    // Kronos ("Home UI Polish" -- "Clean layout"): a real, centered card
    // (not the old raw top-left button stack) -- width is fixed so
    // wrapping/alignment stays predictable across real window sizes,
    // matching the same "fixed-width centered card" shape
    // drawErrorPanel() already uses.
    // Kronos ("Home Screen Avatar Preview"): the card now sits left-of-
    // center (not dead-center) so the real avatar preview panel above
    // has real, non-overlapping room on the right -- a real, deliberate
    // two-column Home layout, not a coincidence of leftover space.
    constexpr float kCardWidth = 340.0f;
    float cardX = viewport->WorkPos.x + viewport->WorkSize.x * 0.30f - kCardWidth * 0.5f;
    ImGui::SetCursorPos(ImVec2(cardX - viewport->WorkPos.x, viewport->WorkSize.y * 0.16f));
    ImGui::BeginGroup();

    ImGui::SetWindowFontScale(2.2f);
    // Real, honest default font -- no custom font asset shipped for this
    // shell yet (see this comment's own long-standing precedent).
    ImGui::TextUnformatted("KRONOS");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::TextDisabled("Alpha Platform");
    ImGui::Dummy(ImVec2(0.0f, 12.0f));

    ImGui::SetNextItemWidth(kCardWidth);
    if (ImGui::InputText("##playing_as", displayNameBuffer_, sizeof(displayNameBuffer_))) {
        localProfile_.displayName = displayNameBuffer_;
        (void)localProfile_.saveToFile(kLocalProfilePath);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Playing As");
    ImGui::Dummy(ImVec2(0.0f, 8.0f));

    // Primary action, visually distinct (accent color) from every
    // secondary action below it -- the one real, most common thing a
    // returning player wants to do.
    //
    // Kronos ("Base Client UI Theme"): real, vibrant green (was blue) --
    // a real, deliberate, local `PushStyleColor` scoped to just this one
    // button, not a change to the shared `core::applyKronosUITheme()`
    // (which Studio also uses) -- every other button on this screen
    // (Friends/Settings/Sessions/etc.) stays the theme's own neutral
    // default, so "primary action" still reads as visually distinct
    // from "secondary action," just with a different real accent color.
    ImVec2 primaryButtonSize(kCardWidth, 48.0f);
    pushPrimaryActionButtonColors();
    if (ImGui::Button("Game Catalogue", primaryButtonSize)) openGameCatalogue();
    popPrimaryActionButtonColors();
    ImGui::Dummy(ImVec2(0.0f, 10.0f));

    // Kronos ("Social Layer" / "Notifications System"): real, first
    // Home-reachable entry points -- see ShellState::Friends/
    // Notifications' own comments. Badge counts (pending requests/unread)
    // surface directly on the button label so a returning player notices
    // without having to open either panel first.
    std::string friendsLabel = "Friends";
    if (!localProfile_.pendingRequests.empty()) {
        friendsLabel += " (" + std::to_string(localProfile_.pendingRequests.size()) + ")";
    }
    size_t unread = notification::unreadCount(localProfile_);
    std::string notificationsLabel = unread > 0 ? "Notifications (" + std::to_string(unread) + ")" : "Notifications";

    ImVec2 halfButtonSize((kCardWidth - ImGui::GetStyle().ItemSpacing.x) * 0.5f, 40.0f);
    // Kronos ("Marketplace" -- "engine_runtime-side catalogue UI"): the
    // real, first player-facing entry point into the shared
    // avatar-item Marketplace -- see ShellState::AvatarShop's own header
    // comment for why this closes a real, previously-stated gap.
    //
    // Kronos ("Fix Home Screen Layout"): the real, standalone "Sessions"
    // button is removed from this grid -- session browsing now lives
    // entirely inside the Game Catalogue (each real game card's own
    // "Live Sessions" button, see drawGameCard()'s own comment), not a
    // second, separate entry point. showSessionBrowser()/
    // ShellState::SessionBrowser/drawSessionBrowserPanel() themselves
    // are unchanged and still real (ui.sessionBrowser()'s own real Lua
    // binding still reaches them, see ScriptUiApi.hpp) -- only this
    // Home-screen button is gone.
    if (ImGui::Button("Avatar Shop", halfButtonSize)) openAvatarShop();
    ImGui::SameLine();
    if (ImGui::Button(friendsLabel.c_str(), halfButtonSize)) openFriends();
    if (ImGui::Button(notificationsLabel.c_str(), halfButtonSize)) openNotifications();
    ImGui::SameLine();
    if (ImGui::Button("Settings", halfButtonSize)) openSettings();
    // Kronos ("Game Catalogue Overhaul"): the real replacement for the
    // old bare "Play" button plus the old disabled Create/Plugins/Assets
    // placeholders -- Launch Studio genuinely opens the real editor
    // (core::launchProcess(), a real sibling process, not a stub). The
    // real, only path to AvatarEditor/Creator Dashboard, both of which
    // stay real, Studio-only panels by original design (not a gap --
    // see studio::plugins::AvatarEditor/CreatorDashboardPanel), not
    // duplicated here.
    // Kronos ("Base Client UI Theme"): real, same primary-action green
    // as "Game Catalogue" above -- "Launch" is the other real primary
    // action this screen offers.
    pushPrimaryActionButtonColors();
    if (ImGui::Button("Launch Studio", halfButtonSize)) launchStudio();
    popPrimaryActionButtonColors();

    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    ImGui::TextDisabled("Kronos %s", core::kKronosVersion);
    ImGui::SameLine();
    if (ImGui::SmallButton("About")) showAboutOverlay_ = true;

    ImGui::EndGroup();

    ImGui::End();
}

void RuntimeShell::drawSplashPanel() {
    // Kronos ("Branding + Release Prep" -- "logo placeholder"): a real,
    // honest text wordmark, not an image -- there is no real image-asset
    // pipeline anywhere in this codebase yet (the same, already-stated
    // "flat color swatch, not a rendered thumbnail" gap every card/
    // thumbnail in this engine already states), so a placeholder "logo"
    // here means real, deliberate typography, not a fabricated graphic.
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin("##splash", nullptr,
                  ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                      ImGuiWindowFlags_NoInputs);

    // Kronos ("Enable Initial Hourglass Boot Overlay"): real, same
    // animated hourglass drawLoadingPanel() already shows for a network
    // join/local game load -- this is the "application boot" instance
    // of the same real loading beat, not a separate, differently-shaped
    // one. Positioned above the wordmark below.
    ImVec2 hourglassCenter(viewport->WorkSize.x * 0.5f, viewport->WorkSize.y * 0.30f);
    drawAnimatedHourglass(ImGui::GetWindowDrawList(), hourglassCenter, 22.0f, 30.0f, splashClock_);

    ImGui::SetCursorPos(ImVec2(viewport->WorkSize.x * 0.5f - 90.0f, viewport->WorkSize.y * 0.42f));
    ImGui::BeginGroup();
    ImGui::SetWindowFontScale(2.6f);
    ImGui::TextColored(ImVec4(0.28f, 0.55f, 0.95f, 1.0f), "KRONOS");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::TextDisabled("Alpha %s", core::kKronosVersion);
    ImGui::EndGroup();
    ImGui::End();
}

void RuntimeShell::drawAboutPanel() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f - 180.0f,
                                    viewport->WorkPos.y + viewport->WorkSize.y * 0.3f),
                             ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(ImVec2(360.0f, 0.0f), ImGuiCond_Appearing);
    bool open = true;
    if (ImGui::Begin("About Kronos", &open, ImGuiWindowFlags_NoCollapse)) {
        ImGui::TextColored(ImVec4(0.28f, 0.55f, 0.95f, 1.0f), "KRONOS");
        ImGui::Text("Version %s", core::kKronosVersion);
        ImGui::TextDisabled("Built %s", core::kKronosBuildDate);
        ImGui::Separator();
        ImGui::TextWrapped(
            "Kronos is a local, solo-developer game platform: a real-time 3D engine, a Studio "
            "editor, and a runtime shell with an avatar marketplace, social layer, and LAN "
            "multiplayer -- all in one Alpha build.");
        ImGui::Separator();
        ImGui::TextDisabled("Your Profile Id: %s", localProfile_.creatorId.c_str());
        if (ImGui::Button("Close")) open = false;
    }
    ImGui::End();
    if (!open) showAboutOverlay_ = false;
}

void RuntimeShell::drawSessionBrowserPanel() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin("Session Browser", nullptr,
                  ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);

    if (ImGui::Button("Back")) {
        lanBrowser_.stop();
        lanBrowserRunning_ = false;
        state_ = computeNextState(state_, ShellEvent::ReturnHome);
        ImGui::End();
        return;
    }

    ImGui::SeparatorText("Sessions on your network");
    ensureLocalProfileLoaded();
    // Kronos ("Session Browser Polish v2" -- "Sorting"/"Filters"): real,
    // pure logic (net::SessionBrowserSort.hpp) -- this panel only
    // supplies the UI state (sortIndex/friendsOnly) and applies the safe-
    // to-view filter that already existed (Minor/Unknown never even sees
    // an Unsafe row -- unchanged behavior, just moved ahead of sort so
    // both real filters compose cleanly into one final list).
    static constexpr const char* kSessionSortNames[] = {"Most Active", "Newly Created", "Alphabetical"};
    static constexpr net::SessionSortOrder kSessionSortValues[] = {
        net::SessionSortOrder::MostActive, net::SessionSortOrder::NewlyCreated, net::SessionSortOrder::Alphabetical};
    ImGui::SetNextItemWidth(160.0f);
    ImGui::Combo("Sort", &sessionBrowserSortIndex_, kSessionSortNames, IM_ARRAYSIZE(kSessionSortNames));
    ImGui::SameLine();
    ImGui::Checkbox("Friends' sessions only", &sessionBrowserFriendsOnly_);

    std::vector<net::DiscoveredSession> discovered = lanBrowser_.discoveredSessions();
    std::vector<net::DiscoveredSession> safeToView;
    for (const auto& session : discovered) {
        auto safetyStatus = static_cast<core::GameSafetyStatus>(session.gameSafetyStatusValue);
        // Kronos ("Catalogue Safety Integration"): the real, shared rule
        // -- a Minor/Unknown viewer never even sees an Unsafe session row
        // at all (matches the Catalogue's own "hides unsafe games from
        // minors" behavior, not just a disabled Join button they could
        // try to bypass).
        if (core::isGameSafeToLaunchForAgeGroup(safetyStatus, effectiveAgeGroup())) safeToView.push_back(session);
    }
    if (sessionBrowserFriendsOnly_) safeToView = net::filterDiscoveredSessionsToFriends(safeToView, localProfile_.friends);
    std::vector<net::DiscoveredSession> shown =
        net::sortDiscoveredSessions(std::move(safeToView), kSessionSortValues[sessionBrowserSortIndex_]);

    if (shown.empty()) {
        ImGui::TextDisabled(sessionBrowserFriendsOnly_ ? "None of your friends have a session running right now."
                                                        : "Searching for real sessions being announced on your LAN...");
    } else {
        // Kronos ("Moderation Architecture v2", "Session Browser Game
        // Identity"): a real "Game" column -- a flat color-swatch
        // thumbnail (same real, honest "no image pipeline exists"
        // convention as the Game Catalogue's own cards, drawGameCard()
        // above) plus the real broadcast game name, and a real "Unsafe"
        // badge for a self-declared Adult viewer who can still see (but
        // must be warned about) a session Minor Mode would otherwise
        // hide entirely.
        if (ImGui::BeginTable("DiscoveredSessions", 6,
                               ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Name");
            ImGui::TableSetupColumn("Game");
            ImGui::TableSetupColumn("Host");
            ImGui::TableSetupColumn("Players");
            ImGui::TableSetupColumn("Ping");
            ImGui::TableSetupColumn("");
            ImGui::TableHeadersRow();
            // Kronos ("Session Browser Polish v2" -- "Virtualized
            // scrolling"): real ImGuiListClipper, same real convention
            // studio::plugins::CataloguePanel's own grid already
            // establishes -- a real LAN typically surfaces a handful of
            // sessions, but this keeps the row cost O(visible rows) if a
            // future test/demo LAN ever announces hundreds at once.
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(shown.size()));
            while (clipper.Step()) {
                for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                    const net::DiscoveredSession& session = shown[static_cast<size_t>(row)];
                    auto safetyStatus = static_cast<core::GameSafetyStatus>(session.gameSafetyStatusValue);

                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%s", session.sessionName.empty() ? "(unnamed session)" : session.sessionName.c_str());
                    ImGui::TableSetColumnIndex(1);
                    const glm::vec4& c = session.gameThumbnailColor;
                    ImGui::ColorButton("##thumb", ImVec4(c.x, c.y, c.z, c.w),
                                        ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop,
                                        ImVec2(14.0f, 14.0f));
                    ImGui::SameLine();
                    ImGui::Text("%s", session.gameName.empty() ? "(unknown game)" : session.gameName.c_str());
                    if (safetyStatus == core::GameSafetyStatus::UnderReview) {
                        ImGui::SameLine();
                        ImGui::TextColored(ImVec4(0.9f, 0.75f, 0.2f, 1.0f), "Under Review");
                    } else if (safetyStatus == core::GameSafetyStatus::Unsafe) {
                        ImGui::SameLine();
                        ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1.0f), "Unsafe");
                    }
                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%s", session.hostDisplayName.c_str());
                    ImGui::TableSetColumnIndex(3);
                    ImGui::Text("%u / %u", session.currentPlayerCount, session.maxPlayerCount);
                    ImGui::TableSetColumnIndex(4);
                    if (session.pingMs > 0.0f) {
                        ImGui::Text("%.0f ms", static_cast<double>(session.pingMs));
                    } else {
                        ImGui::TextDisabled("--");
                    }
                    ImGui::TableSetColumnIndex(5);
                    ImGui::PushID(static_cast<int>(session.sessionId));
                    // Kronos ("UI Theme Cleanup" -- "green accent
                    // buttons"): real, same shared primary-action green
                    // as Home's own Game Catalogue/Launch Studio buttons
                    // -- Join is this row's own real primary action.
                    pushPrimaryActionButtonColors();
                    if (ImGui::SmallButton("Join")) joinSession(session);
                    popPrimaryActionButtonColors();
                    ImGui::PopID();
                }
            }
            ImGui::EndTable();
        }
    }

    ensureSessionHistoryLoaded();
    std::vector<net::SessionHistoryEntry> recent = sessionHistory_.entriesMostRecentFirst();
    if (!recent.empty()) {
        ImGui::SeparatorText("Recently played");
        for (const auto& entry : recent) {
            ImGui::PushID(entry.address.c_str());
            ImGui::PushID(entry.port);
            if (entry.label.empty()) {
                ImGui::Text("%s:%u", entry.address.c_str(), entry.port);
            } else {
                ImGui::Text("%s (%s:%u)", entry.label.c_str(), entry.address.c_str(), entry.port);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Join##recent")) {
                net::DiscoveredSession manual;
                manual.sessionName = entry.label;
                manual.sourceAddress = entry.address;
                manual.gamePort = entry.port;
                joinSession(manual);
            }
            ImGui::PopID();
            ImGui::PopID();
        }
    }

    ImGui::End();
}

namespace {
// Kronos ("Game Catalogue Overhaul", Phase 5): one real card -- title,
// a flat color-swatch thumbnail (core::GameManifest::thumbnailColor --
// the same honest "no image pipeline exists" answer
// studio::plugins::CataloguePanel's own item cards already give, see
// that class's header comment), truncated description, genre tags,
// real recent-player count (from the real local play log, not a
// fabricated live online count -- this is a local Alpha), and a real
// QualityScore badge. Returns true if this card's own "Play" was
// clicked.
constexpr float kCardWidth = 220.0f;
constexpr float kCardHeight = 170.0f;

// Kronos ("Merged Game Catalogue & Sessions View"): real result --
// either the card's own "Play" button (launch this game locally) or a
// specific live session picked from the card's own expanded session
// list (join it directly), never both from a single card interaction.
struct GameCardResult {
    const core::GameCatalogueEntry* toPlay = nullptr;
    const net::DiscoveredSession* toJoin = nullptr;
};

GameCardResult drawGameCard(const core::GameCatalogueEntry& game,
                             const std::vector<net::DiscoveredSession>& allDiscoveredSessions) {
    GameCardResult result;
    ImGui::PushID(game.manifestPath.c_str());
    ImGui::BeginGroup();

    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const glm::vec4& c = game.manifest.thumbnailColor;
    drawList->AddRectFilled(origin, ImVec2(origin.x + kCardWidth, origin.y + 90.0f),
                             IM_COL32(static_cast<int>(c.x * 255.0f), static_cast<int>(c.y * 255.0f),
                                      static_cast<int>(c.z * 255.0f), static_cast<int>(c.w * 255.0f)));
    ImGui::Dummy(ImVec2(kCardWidth, 90.0f));

    ImGui::TextWrapped("%s", game.manifest.name.c_str());
    if (!game.manifest.description.empty()) {
        ImGui::PushTextWrapPos(origin.x + kCardWidth);
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s", game.manifest.description.c_str());
        ImGui::PopTextWrapPos();
    }
    if (!game.manifest.genreTags.empty()) {
        std::string tags;
        for (size_t i = 0; i < game.manifest.genreTags.size(); ++i) {
            if (i > 0) tags += ", ";
            tags += game.manifest.genreTags[i];
        }
        ImGui::TextDisabled("%s", tags.c_str());
    }
    ImGui::Text("Quality %.2f", static_cast<double>(game.qualityScore));
    ImGui::SameLine();
    ImGui::TextDisabled("%lld played", static_cast<long long>(game.launchCount));

    // Kronos ("Moderation Architecture v2", "Catalogue Safety
    // Integration"): a real, honest badge -- a viewer only ever reaches
    // this card for an Unsafe game at all if they're a self-declared
    // Adult (openGameCatalogue() already real-filters it out otherwise),
    // so seeing this badge here is real, not a dead code path.
    if (game.manifest.safetyStatus == core::GameSafetyStatus::UnderReview) {
        ImGui::TextColored(ImVec4(0.9f, 0.75f, 0.2f, 1.0f), "Under Review");
    } else if (game.manifest.safetyStatus == core::GameSafetyStatus::Unsafe) {
        ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1.0f), "Unsafe");
    }

    // Kronos ("UI Theme Cleanup" -- "green accent buttons"): real, same
    // shared primary-action green as Home's own Game Catalogue/Launch
    // Studio buttons -- Play is this card's own real primary action.
    pushPrimaryActionButtonColors();
    if (ImGui::Button("Play", ImVec2(kCardWidth, 0.0f))) result.toPlay = &game;
    popPrimaryActionButtonColors();

    // Kronos ("Merged Game Catalogue & Sessions View"): real, live
    // sessions currently running *this* game -- filtered from the full
    // discovery list by the same real gameName identity
    // LanSessionAnnouncement/DiscoveredSession already carry (see those
    // structs' own comments), matched against this card's own
    // core::GameManifest::name (the same identity key
    // net::GamePlayLog/core::HiddenGemsSelector already use). A real,
    // honest "no sessions" state when the count is 0 -- no fabricated
    // placeholder rows.
    std::vector<const net::DiscoveredSession*> liveSessions;
    for (const auto& session : allDiscoveredSessions) {
        if (session.gameName == game.manifest.name) liveSessions.push_back(&session);
    }
    ImGui::BeginDisabled(liveSessions.empty());
    if (ImGui::Button(liveSessions.empty() ? "No live sessions" : "Live Sessions", ImVec2(kCardWidth, 0.0f))) {
        ImGui::OpenPopup("LiveSessions");
    }
    ImGui::EndDisabled();
    if (!liveSessions.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("(%d)", static_cast<int>(liveSessions.size()));
    }
    if (ImGui::BeginPopup("LiveSessions")) {
        ImGui::TextDisabled("Live sessions for %s", game.manifest.name.c_str());
        ImGui::Separator();
        for (const net::DiscoveredSession* session : liveSessions) {
            ImGui::PushID(static_cast<int>(session->sessionId));
            ImGui::Text("%s", session->sessionName.empty() ? session->hostDisplayName.c_str() : session->sessionName.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("%d/%d players", session->currentPlayerCount, session->maxPlayerCount);
            ImGui::SameLine();
            pushPrimaryActionButtonColors();
            if (ImGui::SmallButton("Join")) {
                result.toJoin = session;
                ImGui::CloseCurrentPopup();
            }
            popPrimaryActionButtonColors();
            ImGui::PopID();
        }
        ImGui::EndPopup();
    }

    ImGui::EndGroup();
    ImGui::PopID();
    return result;
}

// One horizontally-scrolling strip of cards -- the real "row" the
// Featured/genre/Hidden-Gems sections below all share, same
// `ImGui::SameLine()`-based wrapping/layout technique
// studio::plugins::CataloguePanel::drawGrid() already established,
// adapted to a fixed-height horizontal strip (a real front-page "row"
// convention) instead of a wrapping grid.
GameCardResult drawGameRow(const std::vector<const core::GameCatalogueEntry*>& games, const char* rowId,
                            const std::vector<net::DiscoveredSession>& allDiscoveredSessions) {
    GameCardResult result;
    ImGui::PushID(rowId);
    ImGui::BeginChild("row", ImVec2(0.0f, kCardHeight + 16.0f), false, ImGuiWindowFlags_HorizontalScrollbar);
    for (size_t i = 0; i < games.size(); ++i) {
        if (i > 0) ImGui::SameLine();
        GameCardResult cardResult = drawGameCard(*games[i], allDiscoveredSessions);
        if (cardResult.toPlay) result.toPlay = cardResult.toPlay;
        if (cardResult.toJoin) result.toJoin = cardResult.toJoin;
    }
    ImGui::EndChild();
    ImGui::PopID();
    return result;
}
} // namespace

void RuntimeShell::drawGameCataloguePanel() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin("Game Catalogue", nullptr,
                  ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);

    if (ImGui::Button("Back")) {
        state_ = computeNextState(state_, ShellEvent::ReturnHome);
        ImGui::End();
        return;
    }

    if (discoveredGames_.empty()) {
        ImGui::TextDisabled(
            "No real games found in games/ -- see docs/QUICKSTART.md for the real games/<Name>/game.gamemanifest layout.");
        ImGui::End();
        return;
    }

    const core::GameCatalogueEntry* toPlay = nullptr;
    const net::DiscoveredSession* toJoin = nullptr;

    // Kronos ("Merged Game Catalogue & Sessions View"): real, built once
    // per frame -- every row below shares this same, real, live list
    // (openGameCatalogue() already started lanBrowser_ ticking). A
    // *copy*, not a reference into lanBrowser_'s own internal state, so
    // the `toJoin` pointer any card below hands back stays valid for the
    // rest of this function even if lanBrowser_'s own list changes on a
    // later tick.
    std::vector<net::DiscoveredSession> liveDiscoveredSessions = lanBrowser_.discoveredSessions();

    // Featured -- real, algorithm-selected: top real QualityScore
    // entries, not raw player count (per the user's own spec).
    std::vector<const core::GameCatalogueEntry*> sortedByQuality;
    sortedByQuality.reserve(discoveredGames_.size());
    for (const auto& g : discoveredGames_) sortedByQuality.push_back(&g);
    std::sort(sortedByQuality.begin(), sortedByQuality.end(),
              [](const core::GameCatalogueEntry* a, const core::GameCatalogueEntry* b) {
                  return a->qualityScore > b->qualityScore;
              });
    std::vector<const core::GameCatalogueEntry*> featured(
        sortedByQuality.begin(), sortedByQuality.begin() + static_cast<long>(std::min<size_t>(5, sortedByQuality.size())));
    ImGui::SeparatorText("Featured");
    {
        GameCardResult rowResult = drawGameRow(featured, "featured", liveDiscoveredSessions);
        if (rowResult.toPlay) toPlay = rowResult.toPlay;
        if (rowResult.toJoin) toJoin = rowResult.toJoin;
    }

    // Genre rows -- one real row per distinct genre tag actually present
    // across the real scanned games, sorted the same way Featured is.
    std::vector<std::string> genres;
    for (const auto& g : discoveredGames_) {
        for (const auto& tag : g.manifest.genreTags) {
            if (std::find(genres.begin(), genres.end(), tag) == genres.end()) genres.push_back(tag);
        }
    }
    for (const auto& genre : genres) {
        std::vector<const core::GameCatalogueEntry*> inGenre;
        for (const auto* g : sortedByQuality) {
            if (std::find(g->manifest.genreTags.begin(), g->manifest.genreTags.end(), genre) !=
                g->manifest.genreTags.end()) {
                inGenre.push_back(g);
            }
        }
        ImGui::SeparatorText(genre.c_str());
        GameCardResult rowResult = drawGameRow(inGenre, genre.c_str(), liveDiscoveredSessions);
        if (rowResult.toPlay) toPlay = rowResult.toPlay;
        if (rowResult.toJoin) toJoin = rowResult.toJoin;
    }

    // Hidden Gems -- real selection (core::selectHiddenGems(),
    // core/HiddenGemsSelector.hpp), the exact same real function
    // studio::StudioApp's own dev-notification check uses, so "eligible
    // for the front page" means the real same thing in both places.
    std::vector<core::HiddenGemCandidate> candidates;
    candidates.reserve(discoveredGames_.size());
    for (const auto& g : discoveredGames_) candidates.push_back(core::HiddenGemCandidate{g.manifest, g.qualityScore, g.launchCount});
    std::vector<core::GameManifest> hiddenGemManifests = core::selectHiddenGems(candidates);
    std::vector<const core::GameCatalogueEntry*> hiddenGems;
    for (const auto& manifest : hiddenGemManifests) {
        for (const auto& g : discoveredGames_) {
            if (g.manifest.name == manifest.name) {
                hiddenGems.push_back(&g);
                break;
            }
        }
    }
    if (!hiddenGems.empty()) {
        ImGui::SeparatorText("Hidden Gems");
        GameCardResult rowResult = drawGameRow(hiddenGems, "hidden_gems", liveDiscoveredSessions);
        if (rowResult.toPlay) toPlay = rowResult.toPlay;
        if (rowResult.toJoin) toJoin = rowResult.toJoin;
    }

    if (toPlay) selectGame(*toPlay);
    if (toJoin) joinSession(*toJoin);

    ImGui::End();
}

namespace {
constexpr const char* kAvatarShopCategoryFilterNames[] = {"Any",   "Head", "Hair",      "Face",           "Torso",
                                                            "Legs",  "Accessory", "LayeredClothing", "Emote",
                                                            "Shoes", "Back", "Bundle"};
constexpr core::AvatarItemCategory kAvatarShopCategoryFilterValues[] = {
    core::AvatarItemCategory::Head,      core::AvatarItemCategory::Hair,  core::AvatarItemCategory::Face,
    core::AvatarItemCategory::Torso,     core::AvatarItemCategory::Legs,  core::AvatarItemCategory::Accessory,
    core::AvatarItemCategory::LayeredClothing, core::AvatarItemCategory::Emote,
    core::AvatarItemCategory::Shoes,     core::AvatarItemCategory::Back,  core::AvatarItemCategory::Bundle,
};
constexpr const char* kAvatarShopSortNames[] = {"Relevance", "Price: Low to High", "Price: High to Low",
                                                 "Newly Published", "Top Rated", "Creator (A-Z)"};
constexpr core::CatalogueSearchFilter::SortOrder kAvatarShopSortValues[] = {
    core::CatalogueSearchFilter::SortOrder::Relevance,      core::CatalogueSearchFilter::SortOrder::PriceLowToHigh,
    core::CatalogueSearchFilter::SortOrder::PriceHighToLow, core::CatalogueSearchFilter::SortOrder::RecencyNewestFirst,
    core::CatalogueSearchFilter::SortOrder::TopRated,       core::CatalogueSearchFilter::SortOrder::CreatorAlphabetical,
};

std::string formatAvatarShopRatingStars(float ratingScore, int32_t ratingCount) {
    if (ratingCount <= 0) return "Not yet rated";
    int filled = std::clamp(static_cast<int>(ratingScore + 0.5f), 0, 5);
    std::string stars;
    for (int i = 0; i < 5; ++i) stars += (i < filled) ? '*' : '-';
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s %.1f (%d)", stars.c_str(), static_cast<double>(ratingScore), ratingCount);
    return buf;
}
} // namespace

void RuntimeShell::openAvatarShop() {
    if (state_ != ShellState::Home) return;
    ensureLocalProfileLoaded();
    ensureAvatarCatalogueLoaded();
    state_ = computeNextState(state_, ShellEvent::OpenAvatarShop);
}

void RuntimeShell::drawAvatarShopPanel() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin("Avatar Shop", nullptr,
                  ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);

    // Kronos ("Marketplace" -- "engine_runtime-side catalogue UI" --
    // live re-equip while InGame): real -- when opened as the InGame HUD
    // overlay (see showAvatarShopOverlay_'s own comment), "Back" closes
    // the overlay and resumes movement input instead of real-transitioning
    // ShellState (there is no "Home" to transition to -- a real game is
    // still live underneath).
    if (ImGui::Button("Back")) {
        if (showAvatarShopOverlay_) {
            showAvatarShopOverlay_ = false;
            avatarShopDetailOpen_ = false;
            app_.setMovementInputSuspended(false);
        } else {
            state_ = computeNextState(state_, ShellEvent::ReturnHome);
        }
        ImGui::End();
        return;
    }
    ImGui::SameLine();
    ImGui::Text("Balance: %lld KronosCredits", static_cast<long long>(localProfile_.kronosCredits));

    // Kronos ("Simple Recommendation Engine"): real, rules-based (recent
    // + high purchases + high rating) -- see
    // marketplace::rankRecommendedItems()'s own header comment for the
    // real, honest "not ML" scope. A real click here logs real, local
    // telemetry (see telemetryQueue_'s own comment) instead of a
    // fabricated one.
    {
        int64_t nowSeconds =
            std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        std::vector<const core::AvatarItemManifest*> allApproved =
            avatarCatalogueIndex_.search(core::CatalogueSearchFilter{});
        std::vector<const core::AvatarItemManifest*> recommended =
            marketplace::rankRecommendedItems(allApproved, nowSeconds, 6);
        if (!recommended.empty()) {
            ImGui::SeparatorText("Recommended");
            for (size_t i = 0; i < recommended.size(); ++i) {
                const core::AvatarItemManifest* item = recommended[i];
                ImGui::PushID(item->item.id.c_str());
                constexpr float kRecCardWidth = 110.0f;
                ImGui::BeginGroup();
                ImVec4 swatch(item->item.baseColor.r, item->item.baseColor.g, item->item.baseColor.b, 1.0f);
                if (ImGui::ColorButton("##rec_swatch", swatch, ImGuiColorEditFlags_NoTooltip,
                                        ImVec2(kRecCardWidth, kRecCardWidth * 0.7f))) {
                    avatarShopDetailItemId_ = item->item.id;
                    avatarShopDetailOpen_ = true;
                    analytics::TelemetryEvent event;
                    event.name = "recommendation_click";
                    event.properties["itemId"] = item->item.id;
                    event.properties["position"] = static_cast<int64_t>(i);
                    telemetryQueue_.push(std::move(event));
                }
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kRecCardWidth);
                ImGui::TextUnformatted(item->item.name.c_str());
                ImGui::PopTextWrapPos();
                ImGui::EndGroup();
                ImGui::PopID();
                if (i + 1 < recommended.size()) ImGui::SameLine();
            }
            ImGui::Separator();
        }
    }

    ImGui::SetNextItemWidth(220.0f);
    ImGui::InputTextWithHint("##avatar_shop_search", "Search by name, tag, or creator...", avatarShopSearchText_,
                              sizeof(avatarShopSearchText_));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160.0f);
    ImGui::Combo("Category", &avatarShopCategoryFilterIndex_, kAvatarShopCategoryFilterNames,
                 IM_ARRAYSIZE(kAvatarShopCategoryFilterNames));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(180.0f);
    ImGui::Combo("Sort", &avatarShopSortOrderIndex_, kAvatarShopSortNames, IM_ARRAYSIZE(kAvatarShopSortNames));

    core::CatalogueSearchFilter filter;
    if (avatarShopCategoryFilterIndex_ > 0) filter.category = kAvatarShopCategoryFilterValues[avatarShopCategoryFilterIndex_ - 1];
    filter.textQuery = avatarShopSearchText_;
    // Kronos ("Marketplace" -- "engine_runtime-side catalogue UI"): real,
    // always Approved-only here -- unlike studio::plugins::CataloguePanel's
    // own "My Items" mode, this is a real, pure player-facing storefront,
    // never a creator's own management view (that's Studio's Creator
    // Dashboard's job).
    filter.moderationStatus = core::AvatarItemModerationStatus::Approved;
    filter.sortOrder = kAvatarShopSortValues[avatarShopSortOrderIndex_];
    std::vector<const core::AvatarItemManifest*> results = avatarCatalogueIndex_.search(filter);

    ImGui::Separator();
    ImGui::TextDisabled("%zu item%s", results.size(), results.size() == 1 ? "" : "s");

    constexpr float kCardWidth = 160.0f;
    float availWidth = ImGui::GetContentRegionAvail().x;
    int columns = std::max(1, static_cast<int>(availWidth / (kCardWidth + ImGui::GetStyle().ItemSpacing.x)));
    int rowCount = static_cast<int>((results.size() + static_cast<size_t>(columns) - 1) / static_cast<size_t>(columns));

    ImGui::BeginChild("avatar_shop_grid");
    if (results.empty()) {
        ImGui::TextDisabled(filter.textQuery.empty() ? "No items in the Marketplace yet."
                                                       : "No items match your search.");
    } else {
        ImGuiListClipper clipper;
        clipper.Begin(rowCount);
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                size_t rowStart = static_cast<size_t>(row) * static_cast<size_t>(columns);
                size_t rowEnd = std::min(rowStart + static_cast<size_t>(columns), results.size());
                for (size_t i = rowStart; i < rowEnd; ++i) {
                    const core::AvatarItemManifest* item = results[i];
                    ImGui::PushID(item->item.id.c_str());
                    ImGui::BeginGroup();
                    ImVec4 swatch(item->item.baseColor.r, item->item.baseColor.g, item->item.baseColor.b, 1.0f);
                    if (ImGui::ColorButton("##swatch", swatch, ImGuiColorEditFlags_NoTooltip,
                                            ImVec2(kCardWidth, kCardWidth * 0.7f))) {
                        avatarShopDetailItemId_ = item->item.id;
                        avatarShopDetailOpen_ = true;
                        // Kronos ("Marketplace Analytics" -- "Opening item
                        // detail popup -> increment views"): real, same
                        // convention studio::plugins::CataloguePanel's own
                        // openDetail() already establishes.
                        core::AvatarItemManifest withView = *item;
                        withView.views += 1;
                        avatarCatalogueDatabase_.upsert(withView);
                        (void)avatarCatalogueDatabase_.saveToFile(kAvatarCatalogueDatabasePath);
                        avatarCatalogueIndex_.upsert(std::move(withView));
                    }
                    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kCardWidth);
                    ImGui::TextUnformatted(item->item.name.c_str());
                    ImGui::PopTextWrapPos();
                    ImGui::TextDisabled("%d KronosCredits", item->price);
                    ImGui::EndGroup();
                    ImGui::PopID();
                    if (i + 1 < rowEnd) ImGui::SameLine();
                }
            }
        }
    }
    ImGui::EndChild();

    ImGui::End();

    drawAvatarShopDetailPopup();
}

void RuntimeShell::drawAvatarShopDetailPopup() {
    if (!avatarShopDetailOpen_) return;
    const core::AvatarItemManifest* entry = avatarCatalogueIndex_.findById(avatarShopDetailItemId_);
    if (entry == nullptr) {
        avatarShopDetailOpen_ = false;
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(420.0f, 320.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Item Detail##avatar_shop", &avatarShopDetailOpen_);

    ImVec4 swatch(entry->item.baseColor.r, entry->item.baseColor.g, entry->item.baseColor.b, 1.0f);
    ImGui::ColorButton("##detail_swatch", swatch, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop,
                       ImVec2(96.0f, 96.0f));
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::Text("%s", entry->item.name.c_str());
    ImGui::TextDisabled("%s -- %d KronosCredits", core::avatarItemCategoryName(entry->item.category), entry->price);
    ImGui::TextDisabled("By %s", entry->creatorId.empty() ? "unknown" : entry->creatorId.c_str());
    ImGui::TextDisabled("%s", formatAvatarShopRatingStars(entry->ratingScore, entry->ratingCount).c_str());
    ImGui::EndGroup();

    if (!entry->item.tags.empty()) {
        std::string tagLine;
        for (size_t i = 0; i < entry->item.tags.size(); ++i) {
            if (i > 0) tagLine += ", ";
            tagLine += entry->item.tags[i];
        }
        ImGui::TextWrapped("Tags: %s", tagLine.c_str());
    }

    ImGui::Separator();

    // Kronos ("Marketplace" -- "engine_runtime-side catalogue UI" --
    // live re-equip while InGame): real -- equipping always saves
    // avatarLoadout_ immediately; while a real avatar is already spawned
    // (state_ == InGame, reached via the "Shop" HUD overlay button, see
    // showAvatarShopOverlay_'s own comment), it also real-live-re-tints
    // that avatar right now via core::Application::
    // refreshLocalPlayerAvatarAppearance() -- the exact same
    // resolveSegmentColorsForLoadout() mechanism studio::plugins::
    // AvatarEditor's own live preview already uses. Opened from Home
    // instead (no avatar spawned yet), the same call is a real, honest
    // no-op, and the new loadout simply takes effect at the next spawn.
    bool isEquipped = avatarLoadout_.equippedItemId(entry->item.category) == entry->item.id;
    if (isEquipped) {
        ImGui::TextDisabled("Equipped");
        if (ImGui::Button("Unequip")) {
            avatarLoadout_.unequip(entry->item.category);
            (void)avatarLoadout_.saveToFile(kAvatarLoadoutPath);
            app_.refreshLocalPlayerAvatarAppearance(core::resolveSkinToneColor(localProfile_.skinToneIndex),
                                                      avatarLoadout_, avatarCatalogueIndex_);
            // Kronos ("Home Screen Avatar Preview"): keeps the Home
            // preview in sync with the same real appearance change that
            // just live-re-tinted the actual in-game avatar above --
            // real no-op if the preview hasn't been constructed yet
            // (Home was never visited this session).
            if (homeAvatarPreview_) homeAvatarPreview_->refresh();
            avatarShopStatusMessage_ = "Unequipped \"" + entry->item.name + "\".";
        }
    } else if (localProfile_.ownsItem(entry->item.id)) {
        if (ImGui::Button("Equip")) {
            if (avatarLoadout_.equip(entry->item.id, avatarCatalogueIndex_)) {
                (void)avatarLoadout_.saveToFile(kAvatarLoadoutPath);
                app_.refreshLocalPlayerAvatarAppearance(core::resolveSkinToneColor(localProfile_.skinToneIndex),
                                                          avatarLoadout_, avatarCatalogueIndex_);
                if (homeAvatarPreview_) homeAvatarPreview_->refresh();
                avatarShopStatusMessage_ = "Equipped \"" + entry->item.name + "\".";
            }
        }
    } else if (ImGui::Button("Purchase")) {
        int64_t now =
            std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        marketplace::CreditsPurchaseResult result =
            marketplace::purchaseItemWithCredits(localProfile_, *entry, transactionLog_, now);
        switch (result.outcome) {
            case marketplace::CreditsPurchaseOutcome::Success: {
                avatarShopStatusMessage_ =
                    "Purchased \"" + entry->item.name + "\" for " + std::to_string(entry->price) + " KronosCredits.";
                notify(core::NotificationKind::ItemPurchase, "Item purchased",
                       "You purchased \"" + entry->item.name + "\" for " + std::to_string(entry->price) +
                           " KronosCredits.",
                       entry->item.id);
                // Kronos ("Simple Recommendation Engine" -- "Track CTR
                // and conversion"): real, local telemetry -- correlating
                // this against a real, prior "recommendation_click" event
                // for the same itemId (by timestamp proximity) is the
                // real, honest way a future analysis pass would compute
                // conversion; no fabricated click-to-purchase attribution
                // is stored here directly.
                {
                    analytics::TelemetryEvent purchaseEvent;
                    purchaseEvent.name = "item_purchased";
                    purchaseEvent.properties["itemId"] = entry->item.id;
                    purchaseEvent.properties["priceCredits"] = static_cast<int64_t>(entry->price);
                    telemetryQueue_.push(std::move(purchaseEvent));
                }
                (void)localProfile_.saveToFile(kLocalProfilePath);
                (void)transactionLog_.saveToFile(kTransactionLogPath);
                core::AvatarItemManifest withPurchase = *entry;
                withPurchase.purchaseCount += 1;
                avatarCatalogueDatabase_.upsert(withPurchase);
                (void)avatarCatalogueDatabase_.saveToFile(kAvatarCatalogueDatabasePath);
                avatarCatalogueIndex_.upsert(std::move(withPurchase));
                break;
            }
            case marketplace::CreditsPurchaseOutcome::AlreadyOwned:
                avatarShopStatusMessage_ = "You already own this item.";
                break;
            case marketplace::CreditsPurchaseOutcome::InsufficientCredits:
                avatarShopStatusMessage_ = "Not enough KronosCredits (need " + std::to_string(entry->price) + ", have " +
                                            std::to_string(localProfile_.kronosCredits) + ").";
                break;
        }
    }
    // Kronos ("Ratings Notifications (Creator-side)" -- "Add a real call
    // site in the runtime rating submission path"): real -- this is the
    // one real place a player can actually submit a rating anywhere in
    // this codebase today (Studio's own Rate Item UI was removed when
    // purchasing/rating moved runtime-only, see CataloguePanel.cpp's own
    // history). `creatorProfile` is real, but nullptr at this real call
    // site -- see marketplace::submitRating()'s own header comment for
    // why: this Alpha has exactly one resident LocalProfile per machine,
    // and the CannotRateOwnItem guard already prevents that one profile
    // from ever being both rater and creator, so there's no real second
    // profile object here to pass. The notification logic itself is
    // real and correct (see RatingSubmission.cpp's own tests) for
    // whichever future surface actually has one.
    ImGui::Separator();
    if (localProfile_.hasRatedItem(entry->item.id)) {
        ImGui::TextDisabled("You've already rated this item.");
    } else if (!localProfile_.creatorId.empty() && localProfile_.creatorId == entry->creatorId) {
        ImGui::TextDisabled("You can't rate your own item.");
    } else {
        ImGui::TextDisabled("Rate this item:");
        for (int star = 1; star <= 5; ++star) {
            ImGui::SameLine();
            ImGui::PushID(star);
            if (ImGui::SmallButton(std::to_string(star).c_str())) {
                core::AvatarItemManifest mutableEntry = *entry;
                marketplace::RatingSubmissionResult result =
                    marketplace::submitRating(localProfile_, mutableEntry, static_cast<float>(star), nullptr);
                if (result.succeeded()) {
                    (void)localProfile_.saveToFile(kLocalProfilePath);
                    avatarCatalogueDatabase_.upsert(mutableEntry);
                    (void)avatarCatalogueDatabase_.saveToFile(kAvatarCatalogueDatabasePath);
                    avatarCatalogueIndex_.upsert(std::move(mutableEntry));
                    avatarShopStatusMessage_ = "Rated " + std::to_string(star) + " star" + (star == 1 ? "" : "s") + ". Thank you!";
                } else if (result.outcome == marketplace::RatingSubmissionOutcome::AlreadyRated) {
                    avatarShopStatusMessage_ = "You've already rated this item.";
                } else {
                    avatarShopStatusMessage_ = "You can't rate your own item.";
                }
            }
            ImGui::PopID();
        }
    }

    if (!avatarShopStatusMessage_.empty()) ImGui::TextDisabled("%s", avatarShopStatusMessage_.c_str());

    ImGui::End();
    if (!avatarShopDetailOpen_) avatarShopDetailItemId_.clear();
}

namespace {
// Kronos ("Input Remapping System"): the real, exact list of gameplay
// actions this pass makes remappable -- movement/jump/interact (already
// real, bindable core::CharacterController/core::Application actions,
// see those files' own bindAction() calls) plus the two real, new
// OpenChat/OpenShop actions Application::initialize() now also binds
// (previously a hardcoded ImGuiKey_Slash check and a mouse-only HUD
// button respectively). Deliberately does NOT include mouse-look/zoom/
// right-click actions or "Emotes" -- none of those are real, existing
// bindable gameplay mechanics in this engine (mouse-look reads raw
// mouseDelta() directly, not a bound action; there is no zoom/right-click/
// generic-emote-trigger mechanic anywhere in gameplay to remap) -- adding
// rebind UI for a mechanic that doesn't exist would be exactly the kind
// of fabricated capability this codebase's own discipline avoids.
struct RemappableAction {
    const char* actionName; // matches platform_adapters::UnifiedInput's own bindAction() key
    const char* displayLabel;
    int defaultScancode;
};
constexpr RemappableAction kRemappableActions[] = {
    {"MoveForward", "Move Forward", SDL_SCANCODE_W},
    {"MoveBackward", "Move Backward", SDL_SCANCODE_S},
    {"MoveLeft", "Strafe Left", SDL_SCANCODE_A},
    {"MoveRight", "Strafe Right", SDL_SCANCODE_D},
    {"Jump", "Jump", SDL_SCANCODE_SPACE},
    {"Interact", "Interact", SDL_SCANCODE_E},
    {"OpenChat", "Chat", SDL_SCANCODE_SLASH},
    {"OpenShop", "Shop", SDL_SCANCODE_B},
};

constexpr const char* kQualityPresetNames[] = {"Low", "Medium", "High"};
constexpr const char* kColorblindModeNames[] = {"None", "Protanopia", "Deuteranopia", "Tritanopia"};
} // namespace

void RuntimeShell::applyQualityPreset(int presetIndex) {
    core::Renderer& renderer = app_.renderer();
    switch (std::clamp(presetIndex, 0, 2)) {
        case 0: // Low
            renderer.setPerformanceMode(true);
            renderer.setSSREnabled(false);
            renderer.setVolumetricFogEnabled(false);
            renderer.setRTReflectionsEnabled(false);
            renderer.setRTGIEnabled(false);
            break;
        case 2: // High
            renderer.setCinematicMode(true); // real side effect: disables performance mode
            // Kronos ("Critical Visual Fixes" -- "High Quality Graphics
            // Blurriness"): real, explicit -- Cinematic Mode's own DOF
            // pass is opt-in now (see Renderer::setDepthOfFieldEnabled()'s
            // own comment); this preset wants Cinematic Mode's other
            // effects (SSAO, motion blur, auto-exposure) but never tuned
            // DOF for real gameplay camera distances, so it stays off
            // here rather than silently inheriting class-default params
            // meant for a trailer-scene distance.
            renderer.setDepthOfFieldEnabled(false);
            renderer.setSSREnabled(true);
            renderer.setVolumetricFogEnabled(true);
            renderer.setRTReflectionsEnabled(true);
            renderer.setRTGIEnabled(true);
            break;
        default: // Medium
            renderer.setPerformanceMode(false);
            renderer.setCinematicMode(false);
            renderer.setSSREnabled(true);
            renderer.setVolumetricFogEnabled(true);
            renderer.setRTReflectionsEnabled(false);
            renderer.setRTGIEnabled(false);
            break;
    }
}

void RuntimeShell::applyInputBindingOverrides() {
    for (const auto& action : kRemappableActions) {
        int scancode = action.defaultScancode;
        auto it = localProfile_.inputBindingOverrides.find(action.actionName);
        if (it != localProfile_.inputBindingOverrides.end()) scancode = it->second;
        app_.input().clearBindings(action.actionName);
        app_.input().bindAction(
            action.actionName,
            platform_adapters::InputBinding{platform_adapters::PhysicalInputKind::KeyboardKey, scancode});
    }
}

void RuntimeShell::applyAllSettingsFromProfile() {
    applyQualityPreset(localProfile_.qualityPresetIndex);
    // Kronos ("Graphics Setting -- Volumetric Fog Toggle"): real, explicit
    // override -- applied *after* applyQualityPreset() above so a
    // player's own choice here always wins over whatever that preset
    // bundles by default (see LocalProfile::volumetricFogEnabled's own
    // comment).
    app_.renderer().setVolumetricFogEnabled(localProfile_.volumetricFogEnabled);
    app_.renderer().setVsyncEnabled(localProfile_.vsyncEnabled);
    if (app_.gameLoop() != nullptr) {
        app_.gameLoop()->setTargetRenderDt(localProfile_.fpsCap > 0 ? 1.0f / static_cast<float>(localProfile_.fpsCap)
                                                                     : 0.0f);
    }

    app_.audio().setMasterVolume(localProfile_.masterVolume);
    app_.audio().setCategoryVolume(core::AudioCategory::Music, localProfile_.musicVolume);
    app_.audio().setCategoryVolume(core::AudioCategory::SFX, localProfile_.sfxVolume);

    // Kronos ("UI scale must affect all panels"): real -- ImGui is one
    // global context shared by every real panel this shell draws (Home,
    // Shop, Session Browser, Catalogue [Game Catalogue], chat, this
    // Settings panel itself), so one real style/font-scale change here
    // genuinely applies everywhere at once, not per-panel. Studio's
    // AvatarEditor is a *separate* ImGui context (studio::StudioApp's
    // own) -- see this session's own status report for why that one
    // isn't covered by this same call.
    ImGuiIO& io = ImGui::GetIO();
    io.FontGlobalScale = std::max(localProfile_.textScale, 0.1f);
    ImGui::GetStyle() = baseUIStyle_;
    ImGui::GetStyle().ScaleAllSizes(std::max(localProfile_.uiScale, 0.1f));

    app_.renderer().setColorblindMode(localProfile_.colorblindModeIndex);
    app_.setReducedMotionEnabled(localProfile_.reducedMotion);

    applyInputBindingOverrides();
}

void RuntimeShell::openSettings() {
    if (state_ != ShellState::Home) return;
    ensureLocalProfileLoaded();
    state_ = computeNextState(state_, ShellEvent::OpenSettings);
}

void RuntimeShell::drawSettingsPanel() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin("Settings", nullptr,
                  ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);

    if (ImGui::Button("Back")) {
        if (showSettingsOverlay_) {
            showSettingsOverlay_ = false;
            app_.setMovementInputSuspended(false);
        } else {
            state_ = computeNextState(state_, ShellEvent::ReturnHome);
        }
        rebindingActionName_.clear();
        ImGui::End();
        return;
    }

    bool changed = false;

    ImGui::SeparatorText("Graphics");
    if (ImGui::Combo("Quality Preset", &localProfile_.qualityPresetIndex, kQualityPresetNames,
                      IM_ARRAYSIZE(kQualityPresetNames))) {
        applyQualityPreset(localProfile_.qualityPresetIndex);
        // Kronos ("Graphics Setting -- Volumetric Fog Toggle"): real --
        // applyQualityPreset() just bundled its own default fog state in
        // with everything else; re-apply the player's own explicit
        // volumetricFogEnabled choice right after so switching presets
        // doesn't silently clobber it.
        app_.renderer().setVolumetricFogEnabled(localProfile_.volumetricFogEnabled);
        changed = true;
    }
    if (ImGui::Checkbox("Volumetric Fog", &localProfile_.volumetricFogEnabled)) {
        app_.renderer().setVolumetricFogEnabled(localProfile_.volumetricFogEnabled);
        changed = true;
    }
    if (ImGui::Checkbox("VSync", &localProfile_.vsyncEnabled)) {
        app_.renderer().setVsyncEnabled(localProfile_.vsyncEnabled);
        changed = true;
    }
    if (ImGui::SliderInt("FPS Cap (0 = uncapped)", &localProfile_.fpsCap, 0, 240)) {
        if (app_.gameLoop() != nullptr) {
            app_.gameLoop()->setTargetRenderDt(localProfile_.fpsCap > 0 ? 1.0f / static_cast<float>(localProfile_.fpsCap)
                                                                         : 0.0f);
        }
        changed = true;
    }
    // Kronos ("Settings Panel v2" -- real, honest scope note): live
    // resolution/fullscreen switching is NOT implemented -- see
    // core::LocalProfile::qualityPresetIndex's own comment for exactly
    // why (core::Window has no runtime resolution/fullscreen setter at
    // all today).
    ImGui::TextDisabled("Resolution/Fullscreen: not yet supported -- core::Window has no runtime mode switch yet.");

    ImGui::SeparatorText("Audio");
    if (ImGui::SliderFloat("Master Volume", &localProfile_.masterVolume, 0.0f, 1.0f)) {
        app_.audio().setMasterVolume(localProfile_.masterVolume);
        changed = true;
    }
    if (ImGui::SliderFloat("Music Volume", &localProfile_.musicVolume, 0.0f, 1.0f)) {
        app_.audio().setCategoryVolume(core::AudioCategory::Music, localProfile_.musicVolume);
        changed = true;
    }
    if (ImGui::SliderFloat("SFX Volume", &localProfile_.sfxVolume, 0.0f, 1.0f)) {
        app_.audio().setCategoryVolume(core::AudioCategory::SFX, localProfile_.sfxVolume);
        changed = true;
    }

    ImGui::SeparatorText("Controls");
    ImGui::TextWrapped("Click a binding, then press any key to rebind it.");
    for (const auto& action : kRemappableActions) {
        ImGui::PushID(action.actionName);
        bool isListening = rebindingActionName_ == action.actionName;
        int currentScancode = action.defaultScancode;
        auto overrideIt = localProfile_.inputBindingOverrides.find(action.actionName);
        if (overrideIt != localProfile_.inputBindingOverrides.end()) currentScancode = overrideIt->second;
        std::string keyLabel = isListening ? "Press a key..."
                                            : SDL_GetScancodeName(static_cast<SDL_Scancode>(currentScancode));
        ImGui::Text("%s", action.displayLabel);
        ImGui::SameLine(220.0f);
        if (ImGui::Button(keyLabel.c_str(), ImVec2(160.0f, 0.0f))) {
            rebindingActionName_ = action.actionName;
        }
        ImGui::PopID();
    }
    if (ImGui::Button("Reset All Bindings to Default")) {
        localProfile_.inputBindingOverrides.clear();
        applyInputBindingOverrides();
        changed = true;
    }
    // Kronos ("Input Remapping System"): real SDL keyboard-state polling
    // -- not an ImGui-specific key-event hook, since we need to capture
    // *any* physical key across the whole real scancode range, not one
    // this frame's ImGui already knows to look for. Skips the frame a
    // rebind was just requested on (avoids the same click that opened
    // "Press a key..." also being read as the new binding).
    if (!rebindingActionName_.empty()) {
        int numKeys = 0;
        const Uint8* keyState = SDL_GetKeyboardState(&numKeys);
        for (int scancode = 0; scancode < numKeys; ++scancode) {
            if (!keyState[scancode]) continue;
            if (scancode == SDL_SCANCODE_ESCAPE) { // real, honest "cancel rebind" escape hatch
                rebindingActionName_.clear();
                break;
            }
            localProfile_.inputBindingOverrides[rebindingActionName_] = scancode;
            rebindingActionName_.clear();
            applyInputBindingOverrides();
            changed = true;
            break;
        }
    }

    ImGui::SeparatorText("Accessibility");
    if (ImGui::SliderFloat("Text Size", &localProfile_.textScale, 0.5f, 2.0f)) {
        ImGui::GetIO().FontGlobalScale = std::max(localProfile_.textScale, 0.1f);
        changed = true;
    }
    if (ImGui::SliderFloat("UI Scale", &localProfile_.uiScale, 0.5f, 2.0f)) {
        ImGui::GetStyle() = baseUIStyle_;
        ImGui::GetStyle().ScaleAllSizes(std::max(localProfile_.uiScale, 0.1f));
        changed = true;
    }
    if (ImGui::Combo("Colorblind Mode", &localProfile_.colorblindModeIndex, kColorblindModeNames,
                      IM_ARRAYSIZE(kColorblindModeNames))) {
        app_.renderer().setColorblindMode(localProfile_.colorblindModeIndex);
        changed = true;
    }
    if (ImGui::Checkbox("Reduced Motion (less camera shake, no cutscene FOV changes)", &localProfile_.reducedMotion)) {
        app_.setReducedMotionEnabled(localProfile_.reducedMotion);
        changed = true;
    }

    if (changed) (void)localProfile_.saveToFile(kLocalProfilePath);

    ImGui::End();
}

void RuntimeShell::notify(core::NotificationKind kind, std::string title, std::string body, std::string relatedId) {
    ToastEntry toast;
    toast.title = title;
    toast.body = body;
    toast.remainingSeconds = kToastDurationSeconds;
    activeToasts_.push_back(std::move(toast));
    notification::push(localProfile_, kind, std::move(title), std::move(body), std::move(relatedId));
}

void RuntimeShell::tickToasts(float dt) {
    for (auto& toast : activeToasts_) toast.remainingSeconds -= dt;
    activeToasts_.erase(std::remove_if(activeToasts_.begin(), activeToasts_.end(),
                                        [](const ToastEntry& t) { return t.remainingSeconds <= 0.0f; }),
                         activeToasts_.end());
}

void RuntimeShell::drawToasts() {
    if (activeToasts_.empty()) return;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    float y = 16.0f;
    for (size_t i = 0; i < activeToasts_.size(); ++i) {
        const ToastEntry& toast = activeToasts_[i];
        ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - 336.0f, viewport->WorkPos.y + y),
                                 ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(320.0f, 0.0f), ImGuiCond_Always);
        // Real, fading-out alpha over the last real second of a toast's
        // life, not just an abrupt disappear -- a small real polish
        // touch, not load-bearing behavior.
        ImGui::SetNextWindowBgAlpha(0.85f * std::clamp(toast.remainingSeconds, 0.0f, 1.0f) + 0.10f);
        std::string windowId = "##toast" + std::to_string(i);
        if (ImGui::Begin(windowId.c_str(), nullptr,
                          ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNav |
                              ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoInputs)) {
            ImGui::TextColored(ImVec4(0.6f, 0.85f, 1.0f, 1.0f), "%s", toast.title.c_str());
            ImGui::TextWrapped("%s", toast.body.c_str());
        }
        ImGui::End();
        y += 78.0f;
    }
}

void RuntimeShell::openFriends() {
    if (state_ != ShellState::Home) return;
    ensureLocalProfileLoaded();
    state_ = computeNextState(state_, ShellEvent::OpenFriends);
}

void RuntimeShell::drawFriendsPanel() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin("Friends", nullptr,
                  ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);

    if (ImGui::Button("Back")) {
        if (showFriendsOverlay_) {
            showFriendsOverlay_ = false;
            app_.setMovementInputSuspended(false);
        } else {
            state_ = computeNextState(state_, ShellEvent::ReturnHome);
        }
        openConversationFriendId_.clear();
        ImGui::End();
        return;
    }

    // Kronos ("Social Layer" -- honesty note, real UI-facing text, not
    // just a code comment): there is no account/server system in this
    // Alpha (see core::LocalProfile's own "no auth" scope) -- a friend
    // id is the real, stable Creator Id shown on someone's Creator
    // Profile / marketplace listing, not a live directory lookup.
    ImGui::TextDisabled("Your Friend Id: %s (share this so someone else can add you)", localProfile_.creatorId.c_str());
    ImGui::Separator();

    ImGui::SeparatorText("Add a Friend");
    ImGui::SetNextItemWidth(220.0f);
    ImGui::InputTextWithHint("##add_friend_id", "Friend Id (e.g. creator_1234567890)", addFriendIdBuffer_,
                              sizeof(addFriendIdBuffer_));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160.0f);
    ImGui::InputTextWithHint("##add_friend_name", "Display Name", addFriendNameBuffer_, sizeof(addFriendNameBuffer_));
    ImGui::SameLine();
    if (ImGui::Button("Send Request") && addFriendIdBuffer_[0] != '\0') {
        social::FriendRequestOutcome outcome =
            social::sendFriendRequest(localProfile_, addFriendIdBuffer_, addFriendNameBuffer_);
        switch (outcome) {
            case social::FriendRequestOutcome::Sent:
                notify(core::NotificationKind::FriendRequest, "Friend request sent",
                       "You sent a friend request to " +
                           std::string(addFriendNameBuffer_[0] != '\0' ? addFriendNameBuffer_ : addFriendIdBuffer_) +
                           ".",
                       addFriendIdBuffer_);
                friendsStatusMessage_ = "Friend request sent.";
                addFriendIdBuffer_[0] = '\0';
                addFriendNameBuffer_[0] = '\0';
                break;
            case social::FriendRequestOutcome::AlreadyFriends:
                friendsStatusMessage_ = "You're already friends.";
                break;
            case social::FriendRequestOutcome::AlreadyPending:
                friendsStatusMessage_ = "You already sent a request to this id.";
                break;
            case social::FriendRequestOutcome::CannotFriendSelf:
                friendsStatusMessage_ = "That's your own Friend Id.";
                break;
        }
        (void)localProfile_.saveToFile(kLocalProfilePath);
    }
    if (!friendsStatusMessage_.empty()) ImGui::TextDisabled("%s", friendsStatusMessage_.c_str());

    if (!localProfile_.pendingRequests.empty()) {
        ImGui::SeparatorText("Pending Requests (sent, awaiting response)");
        // Kronos ("Social Layer" -- honest local-simulation scope): there
        // is no real transport to deliver this request to another
        // machine (see social::sendFriendRequest()'s own header
        // comment). "Simulate Accept"/"Cancel" real-drive the exact
        // state machine a real remote response would, standing in for
        // it -- the panel says so plainly rather than pretending a real
        // delivery happened.
        std::string acceptId, declineId;
        for (const auto& req : localProfile_.pendingRequests) {
            ImGui::PushID(req.friendId.c_str());
            ImGui::Text("%s (%s)", req.displayName.empty() ? req.friendId.c_str() : req.displayName.c_str(),
                        req.friendId.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Simulate Accept")) acceptId = req.friendId;
            ImGui::SameLine();
            if (ImGui::SmallButton("Cancel")) declineId = req.friendId;
            ImGui::PopID();
        }
        if (!acceptId.empty()) {
            (void)social::acceptFriendRequest(localProfile_, acceptId);
            notify(core::NotificationKind::FriendRequest, "New friend", "You are now friends with " + acceptId + ".",
                   acceptId);
            (void)localProfile_.saveToFile(kLocalProfilePath);
        }
        if (!declineId.empty()) {
            (void)social::declineFriendRequest(localProfile_, declineId);
            (void)localProfile_.saveToFile(kLocalProfilePath);
        }
    }

    ImGui::SeparatorText("Friends");
    if (localProfile_.friends.empty()) {
        ImGui::TextDisabled("No friends yet -- send a request above using someone's real Friend Id.");
    }
    std::string removeId;
    for (const auto& friendEntry : localProfile_.friends) {
        ImGui::PushID(friendEntry.friendId.c_str());
        social::PresenceState presence = social::computeFriendPresence(
            friendEntry.displayName, lanBrowserRunning_ ? &lanBrowser_ : nullptr,
            app_.networkSession().isActive() ? &app_.networkSession() : nullptr);
        const char* presenceLabel =
            presence == social::PresenceState::InGame ? "In Game"
            : presence == social::PresenceState::Online ? "Online"
                                                          : "Offline";
        ImVec4 presenceColor = presence == social::PresenceState::InGame ? ImVec4(0.5f, 0.9f, 0.5f, 1.0f)
                                : presence == social::PresenceState::Online ? ImVec4(0.6f, 0.85f, 1.0f, 1.0f)
                                                                             : ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
        ImGui::Text("%s", friendEntry.displayName.empty() ? friendEntry.friendId.c_str() : friendEntry.displayName.c_str());
        ImGui::SameLine();
        ImGui::TextColored(presenceColor, "[%s]", presenceLabel);
        ImGui::SameLine();
        if (ImGui::SmallButton("Message")) openConversationFriendId_ = friendEntry.friendId;
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove")) removeId = friendEntry.friendId;
        ImGui::PopID();
    }
    if (!removeId.empty()) {
        social::removeFriend(localProfile_, removeId);
        if (openConversationFriendId_ == removeId) openConversationFriendId_.clear();
        (void)localProfile_.saveToFile(kLocalProfilePath);
    }

    // Kronos ("Social Layer" -- "Simple messaging overlay (local
    // simulation only)"): real, persisted, local-only chat log with the
    // one currently-selected friend -- see core::FriendMessage's own
    // comment for exactly what "local simulation" honestly means here.
    if (!openConversationFriendId_.empty()) {
        ImGui::SeparatorText(("Conversation with " + openConversationFriendId_).c_str());
        ImGui::BeginChild("friend_conversation", ImVec2(0.0f, 200.0f), true);
        for (const auto& message : social::messagesWithFriend(localProfile_, openConversationFriendId_)) {
            ImGui::TextWrapped("%s: %s", message.fromMe ? "You" : "Them", message.text.c_str());
        }
        ImGui::EndChild();
        ImGui::SetNextItemWidth(400.0f);
        bool sendPressed = ImGui::InputText("##friend_message_input", friendMessageInputBuffer_,
                                             sizeof(friendMessageInputBuffer_), ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        if ((ImGui::Button("Send") || sendPressed) && friendMessageInputBuffer_[0] != '\0') {
            social::sendMessage(localProfile_, openConversationFriendId_, friendMessageInputBuffer_, true);
            friendMessageInputBuffer_[0] = '\0';
            (void)localProfile_.saveToFile(kLocalProfilePath);
        }
    }

    ImGui::End();
}

void RuntimeShell::openNotifications() {
    if (state_ != ShellState::Home) return;
    ensureLocalProfileLoaded();
    state_ = computeNextState(state_, ShellEvent::OpenNotifications);
}

namespace {
constexpr const char* kNotificationFilterNames[] = {"All",     "Friend Requests", "Item Purchases",
                                                       "Ratings", "Moderation",      "System"};
} // namespace

void RuntimeShell::drawNotificationsPanel() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin("Notifications", nullptr,
                  ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);

    if (ImGui::Button("Back")) {
        if (showNotificationsOverlay_) {
            showNotificationsOverlay_ = false;
            app_.setMovementInputSuspended(false);
        } else {
            state_ = computeNextState(state_, ShellEvent::ReturnHome);
        }
        ImGui::End();
        return;
    }
    ImGui::SameLine();
    if (ImGui::Button("Mark All Read")) {
        notification::markAllRead(localProfile_);
        (void)localProfile_.saveToFile(kLocalProfilePath);
    }

    ImGui::SetNextItemWidth(200.0f);
    ImGui::Combo("Filter", &notificationFilterKindIndex_, kNotificationFilterNames,
                 IM_ARRAYSIZE(kNotificationFilterNames));
    ImGui::SameLine();
    ImGui::Checkbox("Unread only", &notificationUnreadOnlyFilter_);

    std::optional<core::NotificationKind> kindFilter;
    if (notificationFilterKindIndex_ > 0) kindFilter = static_cast<core::NotificationKind>(notificationFilterKindIndex_ - 1);
    std::vector<size_t> indices =
        notification::filteredIndicesMostRecentFirst(localProfile_, kindFilter, notificationUnreadOnlyFilter_);

    ImGui::Separator();
    ImGui::TextDisabled("%zu notification%s", indices.size(), indices.size() == 1 ? "" : "s");
    ImGui::BeginChild("notification_list");
    for (size_t index : indices) {
        const core::NotificationRecord& record = localProfile_.notifications[index];
        ImGui::PushID(static_cast<int>(index));
        if (!record.read) ImGui::Bullet();
        ImGui::SameLine();
        ImGui::TextWrapped("%s", record.title.c_str());
        ImGui::TextWrapped("%s", record.body.c_str());
        if (!record.read) {
            if (ImGui::SmallButton("Mark Read")) {
                notification::markRead(localProfile_, index);
                (void)localProfile_.saveToFile(kLocalProfilePath);
            }
        }
        ImGui::Separator();
        ImGui::PopID();
    }
    ImGui::EndChild();

    ImGui::End();
}

void RuntimeShell::drawLoadingPanel() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin("Connecting", nullptr,
                  ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);

    // Kronos ("Animated Hourglass Loading Screen"): real, shared by both
    // real Loading sources -- a network join (unchanged text) and a real
    // local game load (pendingGameLoad_, "Animated Hourglass Loading
    // Screen" -- "scene loads"), which now shows the picked game's own
    // real name instead of a generic "session".
    ImVec2 hourglassCenter(viewport->WorkSize.x * 0.5f, viewport->WorkSize.y * 0.42f);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawAnimatedHourglass(drawList, hourglassCenter, 26.0f, 34.0f, stateTransitionClock_);

    ImGui::SetCursorPos(ImVec2(viewport->WorkSize.x * 0.5f - 100.0f, viewport->WorkSize.y * 0.42f + 50.0f));
    ImGui::BeginGroup();
    // Kronos ("Session Browser Polish v2" -- "Join feedback (loading
    // indicator)"): a real, cheap animated dot count driven by the same
    // real stateTransitionClock_ every panel's fade-in already uses --
    // not just a static "Connecting..." string sitting there indefinitely.
    int dotCount = 1 + static_cast<int>(stateTransitionClock_ * 2.0f) % 3;
    std::string dots(static_cast<size_t>(dotCount), '.');
    if (pendingGameLoad_.has_value()) {
        ImGui::Text("Loading %s%s", pendingGameLoad_->manifest.name.c_str(), dots.c_str());
    } else {
        ImGui::Text("Connecting to %s%s", lastJoinedHostDisplayName_.empty() ? "session" : lastJoinedHostDisplayName_.c_str(),
                    dots.c_str());
    }
    if (ImGui::Button("Cancel")) cancelJoin();
    ImGui::EndGroup();

    ImGui::End();
}

void RuntimeShell::drawErrorPanel() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin("Error", nullptr,
                  ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);

    ImGui::SetCursorPos(ImVec2(viewport->WorkSize.x * 0.5f - 200.0f, viewport->WorkSize.y * 0.4f));
    ImGui::BeginGroup();
    switch (lastError_.kind) {
        case ShellErrorKind::JoinFailed:
            ImGui::Text("Couldn't join: %s", joinFailureReasonLabel(lastError_.joinFailureReason));
            if (lastError_.joinFailureReason == net::JoinFailureReason::VersionMismatch) {
                ImGui::Text("This build speaks protocol version %u; the server is on version %u.",
                            net::kNetworkProtocolVersion, lastError_.joinFailureServerProtocolVersion);
            }
            break;
        case ShellErrorKind::Disconnected:
            ImGui::Text("Disconnected: %s", disconnectReasonLabel(lastError_.disconnectReason));
            break;
        case ShellErrorKind::NetworkFailure:
            ImGui::Text("Network error: %s", lastError_.detail.c_str());
            break;
        case ShellErrorKind::None:
            ImGui::Text("An unknown error occurred.");
            break;
    }
    ImGui::Spacing();
    if (ImGui::Button("Back to Home", ImVec2(200.0f, 40.0f))) {
        lastError_ = ShellErrorInfo{};
        state_ = computeNextState(state_, ShellEvent::ReturnHome);
    }
    ImGui::EndGroup();

    ImGui::End();
}

void RuntimeShell::drawPlayerListOverlay() {
    net::NetworkSession& session = app_.networkSession();
    bool online = session.isActive();

    // Always-visible, minimal HUD -- a real "leave" action must always be
    // reachable while InGame, not hidden behind a toggle. Drawn for
    // offline play too now (previously an early-return here left offline
    // play with no HUD -- and therefore no discoverable way back to Home
    // -- at all; see tick()'s own Escape handling for the actual fix,
    // this text is what makes it discoverable without reading docs).
    ImGui::SetNextWindowPos(ImVec2(16.0f, 16.0f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.65f);
    if (ImGui::Begin("##InGameHud", nullptr,
                      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoNav |
                          ImGuiWindowFlags_NoFocusOnAppearing)) {
        ImGui::Text("%s", online ? (session.isServer() ? "Hosting" : "Connected") : "Offline");
        if (online) {
            if (ImGui::Button("Leave Session")) leaveSession();
            ImGui::SameLine();
            if (ImGui::Button(showPlayerListOverlay_ ? "Hide Players" : "Show Players")) {
                showPlayerListOverlay_ = !showPlayerListOverlay_;
            }
        } else {
            if (ImGui::Button("Back to Home")) leaveSession();
        }
        ImGui::SameLine();
        // Kronos ("Critical Fix -- Chat Activation"): real, shown offline
        // too now -- see tickChatActivation()'s own comment for why the
        // "/" keybind and this button used to be online-only, and
        // drawChatPanel()'s own comment for how offline "send" behaves.
        if (ImGui::Button("Chat (/)")) {
            showChatPanel_ = true;
            chatPanelJustOpened_ = true;
            app_.setMovementInputSuspended(true);
        }
        ImGui::SameLine();
        // Kronos ("Marketplace" -- "engine_runtime-side catalogue UI" --
        // live re-equip while InGame): real -- the same Avatar Shop
        // Home's own button opens, drawn here as a real overlay instead
        // (see showAvatarShopOverlay_'s own comment for why this is a
        // separate real flag, not a ShellState transition).
        if (ImGui::Button("Shop")) {
            ensureLocalProfileLoaded();
            ensureAvatarCatalogueLoaded();
            showAvatarShopOverlay_ = true;
            app_.setMovementInputSuspended(true);
        }
        ImGui::SameLine();
        // Kronos ("Settings Panel v2 + Input Remapping + Accessibility
        // Layer" -- "reachable from Home and in-game pause menu"): real,
        // same overlay pattern as the "Shop" button just above.
        if (ImGui::Button("Settings")) {
            ensureLocalProfileLoaded();
            showSettingsOverlay_ = true;
            app_.setMovementInputSuspended(true);
        }
        ImGui::SameLine();
        if (ImGui::Button("Friends")) {
            ensureLocalProfileLoaded();
            showFriendsOverlay_ = true;
            app_.setMovementInputSuspended(true);
        }
        ImGui::SameLine();
        size_t unread = notification::unreadCount(localProfile_);
        std::string notifButtonLabel = unread > 0 ? "Notifications (" + std::to_string(unread) + ")" : "Notifications";
        if (ImGui::Button(notifButtonLabel.c_str())) {
            ensureLocalProfileLoaded();
            showNotificationsOverlay_ = true;
            app_.setMovementInputSuspended(true);
        }
        ImGui::TextDisabled("Press Esc to leave");
    }
    ImGui::End();

    if (!online || !showPlayerListOverlay_) return;

    ImGui::SetNextWindowPos(ImVec2(16.0f, 90.0f), ImGuiCond_Appearing);
    ImGui::SetNextWindowBgAlpha(0.85f);
    if (ImGui::Begin("Session", &showPlayerListOverlay_, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::SeparatorText("Session");
        ImGui::Text("Name: %s", session.sessionName().empty() ? "(unnamed)" : session.sessionName().c_str());
        ImGui::Text("Session ID: %llu", static_cast<unsigned long long>(session.sessionId()));
        ImGui::Text("Protocol version: %u", net::kNetworkProtocolVersion);
        ImGui::Text("Your role: %s", session.isServer() ? "Host" : "Guest");
        if (!session.isServer()) {
            ImGui::Text("Host: %s", lastJoinedHostDisplayName_.empty() ? "(unknown)" : lastJoinedHostDisplayName_.c_str());
        }

        // Kronos ("Active Joining UI"): a real, honest architectural note
        // -- this engine's hosting process owns no PlayerId/roster entry
        // of its own (only remote peers connecting IN get one, see
        // net::NetworkSession::onPeerConnected()'s own comment), so
        // there's no real per-player "is this one the host" flag to show
        // client-side -- every entry a client sees really is a fellow
        // guest. The host/guest distinction that DOES exist (this
        // process's own role) is shown above instead of faking a
        // per-player flag that doesn't correspond to anything real.
        ImGui::SeparatorText("Players");
        if (session.isServer()) {
            for (net::PlayerId player : session.connectedPlayerIds()) {
                core::EntityId entity = session.playerEntity(player);
                const auto* nameComponent =
                    entity != core::kNullEntity ? app_.ecs().tryGetComponent<core::Name>(entity) : nullptr;
                std::string displayName = (nameComponent != nullptr && !nameComponent->value.empty())
                                               ? nameComponent->value
                                               : ("Player" + std::to_string(player));
                ImGui::Text("#%u  %s  (Guest)", player, displayName.c_str());
            }
        } else {
            for (const auto& [player, name] : session.clientKnownPlayers()) {
                bool isLocal = player == session.localPlayerId();
                ImGui::Text("#%u  %s%s", player, name.c_str(), isLocal ? "  (You)" : "");
            }
        }
    }
    ImGui::End();
}

void RuntimeShell::tickChatActivation() {
    // Kronos ("Player & Chat System" -- "Bind the '/' key to open the
    // chat panel"): real, works offline too now (Kronos "Critical Fix --
    // Chat Activation": previously gated to online-only, which made the
    // "/" key and the HUD's own "Chat (/)" button silently do nothing
    // during offline Catalogue play -- a real, confusing dead keybind
    // rather than an honest no-op; see drawChatPanel()'s own comment for
    // how an offline "send" is now a real local echo instead of a
    // network call that would have gone nowhere). Only while no other
    // real ImGui text field already wants keyboard text (WantTextInput)
    // -- without that guard, typing "/" into some *other* hypothetical
    // text field would also real-open chat underneath it, a real,
    // confusing double input this guard prevents.
    if (showChatPanel_) return;
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) return;
    // Kronos ("Input Remapping System"): real -- routed through the real,
    // bindable "OpenChat" action (see Application::initialize()'s own
    // bindAction() call and RuntimeShell::applyInputBindingOverrides())
    // instead of a hardcoded ImGuiKey_Slash check, so a player who
    // rebinds Chat in Settings genuinely opens it with their own chosen
    // key, not always "/".
    bool openChatDown = app_.input().isActionDown("OpenChat");
    if (openChatDown && !openChatKeyWasDown_) {
        showChatPanel_ = true;
        chatPanelJustOpened_ = true;
        app_.setMovementInputSuspended(true);
    }
    openChatKeyWasDown_ = openChatDown;
}

void RuntimeShell::tickEmoteActivation() {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) return; // real -- don't trigger while e.g. typing "g" into the chat box
    bool playEmoteDown = app_.input().isActionDown("PlayEmote");
    if (playEmoteDown && !playEmoteKeyWasDown_) {
        ensureAvatarCatalogueLoaded();
        std::string error;
        bool played = app_.playEquippedEmote(avatarLoadout_, animationDatabase_, error);
        // Kronos ("Notifications System"): real, transient feedback via
        // the same toast mechanism every other real gameplay event
        // already uses -- a status string on the (currently closed)
        // Avatar Shop panel would be invisible to a player who just
        // pressed a gameplay key.
        if (!error.empty()) {
            notify(core::NotificationKind::SystemMessage, "Emote failed", error);
        } else if (!played) {
            notify(core::NotificationKind::SystemMessage, "No emote equipped", "Equip an Emote-category item in the Avatar Shop first.");
        }
    }
    playEmoteKeyWasDown_ = playEmoteDown;
}

void RuntimeShell::drawChatPanel() {
    if (!showChatPanel_) return;

    ImGui::SetNextWindowPos(ImVec2(16.0f, 90.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(420.0f, 260.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.85f);
    bool stillOpen = true;
    if (ImGui::Begin("Chat", &stillOpen)) {
        ImGui::BeginChild("##chat_history", ImVec2(0.0f, -32.0f), true);
        for (const std::string& line : chatHistoryLines_) {
            ImGui::TextWrapped("%s", line.c_str());
        }
        // Real, honest auto-scroll -- only when already at (or starting
        // at) the bottom, so a player who scrolled up to re-read
        // something isn't yanked back down by the next incoming message.
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f) ImGui::SetScrollHereY(1.0f);
        ImGui::EndChild();

        if (chatPanelJustOpened_) {
            ImGui::SetKeyboardFocusHere();
            chatPanelJustOpened_ = false;
        }
        ImGui::SetNextItemWidth(-1.0f);
        bool sent = ImGui::InputText("##chat_input", chatInputBuffer_, sizeof(chatInputBuffer_),
                                      ImGuiInputTextFlags_EnterReturnsTrue);
        if (sent && chatInputBuffer_[0] != '\0') {
            if (app_.networkSession().isActive()) {
                // Kronos ("Player & Chat System" -- "Integrate moderation
                // filters"): real, already-real -- sendChatMessage() routes
                // through the exact same server-side safety::TrustSafetyService/
                // safety::PolicyEngine/moderation::ChatLog pipeline every
                // other chat message (native or scripted) already does; this
                // panel is a real new front door onto real, pre-existing
                // moderation, not a second, parallel path.
                app_.networkSession().sendChatMessage(chatInputBuffer_);
            } else {
                // Kronos ("Critical Fix -- Chat Activation"): real, offline
                // local echo -- NetworkSession::sendChatMessage() itself is
                // already a safe, honest no-op offline (config_.mode !=
                // Client), so routing an offline message through it would
                // just make it silently vanish with no local trace at all,
                // a worse, more confusing UX than not having chat offline
                // in the first place. There's no server to relay it back to
                // this same client (setOnChatMessageReceived() never fires
                // offline either), so this appends directly, matching that
                // callback's own "sender: text" formatting.
                chatHistoryLines_.push_back(
                    (localProfile_.displayName.empty() ? std::string("You") : localProfile_.displayName) + ": " +
                    chatInputBuffer_);
                if (chatHistoryLines_.size() > kMaxChatHistoryLines) chatHistoryLines_.erase(chatHistoryLines_.begin());
            }
            chatInputBuffer_[0] = '\0';
            chatPanelJustOpened_ = true; // real, keeps keyboard focus in the input box for the next message
        }
    }
    ImGui::End();

    // Real Escape-closes -- ImGui's own InputText already consumes
    // Escape to clear an in-progress edit/unfocus itself first, so this
    // real, explicit key check (not ImGui::IsItemDeactivated() or
    // similar) is what actually closes the whole panel on a second,
    // deliberate Escape press, same "Escape backs out one real layer"
    // convention tick()'s own comment on this feature already states.
    bool escapeClosesChatDown = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
    if (!stillOpen || escapeClosesChatDown) {
        showChatPanel_ = false;
        app_.setMovementInputSuspended(false);
    }
}

void RuntimeShell::tickTrailerCaptureMode(float dt) {
    // Real, raw SDL polling -- same reasoning drawSettingsPanel()'s own
    // real key-capture already uses (this is a meta/dev toggle, not a
    // remappable gameplay action, so it deliberately bypasses
    // UnifiedInput entirely).
    int numKeys = 0;
    const Uint8* keyState = SDL_GetKeyboardState(&numKeys);
    bool f9Down = numKeys > SDL_SCANCODE_F9 && keyState[SDL_SCANCODE_F9] != 0;
    bool f10Down = numKeys > SDL_SCANCODE_F10 && keyState[SDL_SCANCODE_F10] != 0;

    if (f9Down && !trailerF9WasDown_) {
        trailerCaptureModeEnabled_ = !trailerCaptureModeEnabled_;
        trailerSmoothedPoseValid_ = false;
        if (!trailerCaptureModeEnabled_) trailerHudHidden_ = false; // real -- leaving capture mode always restores the HUD
    }
    trailerF9WasDown_ = f9Down;

    if (f10Down && !trailerF10WasDown_ && trailerCaptureModeEnabled_) {
        trailerHudHidden_ = !trailerHudHidden_;
    }
    trailerF10WasDown_ = f10Down;

    if (!trailerCaptureModeEnabled_) return;

    // Kronos ("Trailer Capture Mode" -- "Camera smoothing"): real
    // exponential smoothing toward whatever pose gameplay already wrote
    // to app_.camera() this tick -- runs here (RuntimeShell::tick() is
    // registered as GameLoop's own PreRenderHook, see this class's own
    // header comment) so it's the real, last write before the frame
    // actually renders. Zero effect whenever trailerCaptureModeEnabled_
    // is false (the overwhelming majority of real play), so ordinary
    // camera control is completely unaffected.
    core::Camera& camera = app_.camera();
    if (!trailerSmoothedPoseValid_) {
        trailerSmoothedCamera_ = camera;
        trailerSmoothedPoseValid_ = true;
        return;
    }
    float tau = std::max(trailerCameraSmoothingSeconds_, 0.001f);
    float alpha = 1.0f - std::exp(-dt / tau);
    trailerSmoothedCamera_.position = glm::mix(trailerSmoothedCamera_.position, camera.position, alpha);
    trailerSmoothedCamera_.yawDegrees += alpha * (camera.yawDegrees - trailerSmoothedCamera_.yawDegrees);
    trailerSmoothedCamera_.pitchDegrees += alpha * (camera.pitchDegrees - trailerSmoothedCamera_.pitchDegrees);
    trailerSmoothedCamera_.rollDegrees += alpha * (camera.rollDegrees - trailerSmoothedCamera_.rollDegrees);
    trailerSmoothedCamera_.verticalFovDegrees +=
        alpha * (camera.verticalFovDegrees - trailerSmoothedCamera_.verticalFovDegrees);
    camera = trailerSmoothedCamera_;
}

void RuntimeShell::drawTrailerCapturePanel() {
    ImGui::SetNextWindowPos(ImVec2(16.0f, 200.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(300.0f, 0.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.85f);
    if (ImGui::Begin("Trailer Capture Mode")) {
        ImGui::TextDisabled("F9: exit capture mode   F10: hide/show this UI");
        ImGui::Checkbox("Hide HUD (F10)", &trailerHudHidden_);
        ImGui::SliderFloat("Camera Smoothing (s)", &trailerCameraSmoothingSeconds_, 0.0f, 1.0f);

        // Kronos ("Trailer Capture Mode" -- "Export presets"): real, but
        // deliberately does NOT change window resolution/fullscreen --
        // core::Window has no runtime mode-switch API (same real,
        // already-stated gap as Settings' own qualityPresetIndex
        // comment). Each preset is a real, named bundle of the settings
        // that ARE real and live-applicable.
        static constexpr const char* kExportPresetNames[] = {"None", "Cinematic (High quality, hidden HUD)",
                                                                "Clean Static (Medium quality, hidden HUD)"};
        if (ImGui::Combo("Export Preset", &trailerExportPresetIndex_, kExportPresetNames,
                          IM_ARRAYSIZE(kExportPresetNames))) {
            if (trailerExportPresetIndex_ == 1) {
                applyQualityPreset(2);
                trailerHudHidden_ = true;
            } else if (trailerExportPresetIndex_ == 2) {
                applyQualityPreset(1);
                trailerHudHidden_ = true;
            }
        }

        ImGui::SeparatorText("Scene Bookmarks");
        ImGui::SetNextItemWidth(150.0f);
        ImGui::InputTextWithHint("##bookmark_name", "Bookmark name", trailerBookmarkNameBuffer_,
                                  sizeof(trailerBookmarkNameBuffer_));
        ImGui::SameLine();
        if (ImGui::SmallButton("Save") && trailerBookmarkNameBuffer_[0] != '\0') {
            trailerBookmarks_.push_back(CameraBookmark{trailerBookmarkNameBuffer_, app_.camera()});
            trailerBookmarkNameBuffer_[0] = '\0';
        }
        int deleteIndex = -1;
        for (int i = 0; i < static_cast<int>(trailerBookmarks_.size()); ++i) {
            ImGui::PushID(i);
            ImGui::Text("%s", trailerBookmarks_[static_cast<size_t>(i)].name.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Recall")) {
                app_.camera() = trailerBookmarks_[static_cast<size_t>(i)].pose;
                trailerSmoothedPoseValid_ = false; // real -- a hard cut shouldn't smooth from the pre-recall pose
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Delete")) deleteIndex = i;
            ImGui::PopID();
        }
        if (deleteIndex >= 0) trailerBookmarks_.erase(trailerBookmarks_.begin() + deleteIndex);
    }
    ImGui::End();
}

} // namespace engine::runtime
