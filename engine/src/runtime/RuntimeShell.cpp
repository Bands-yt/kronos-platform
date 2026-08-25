#include "runtime/RuntimeShell.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

#include <SDL2/SDL.h>
#include <imgui.h>
#include <backends/imgui_impl_sdl2.h>
#include <backends/imgui_impl_vulkan.h>

#include "core/AvatarSkinTone.hpp"
#include "core/CredentialStore.hpp"
#include "core/Components.hpp"
#include "core/HiddenGemsSelector.hpp"
#include "core/KronosVersion.hpp"
#include "core/ProcessLaunch.hpp"
#include "core/Logger.hpp"
#include "core/ResourcePaths.hpp"
#include "core/UpdateCheck.hpp"
#include "core/KronosClientConfig.hpp"
#include "core/LoopbackHttpServer.hpp"
#include "core/OAuthPkce.hpp"
#include "core/OpenUrl.hpp"
#include "core/UITheme.hpp"
#include "marketplace/CreditsPurchase.hpp"
#include "marketplace/RatingSubmission.hpp"
#include "marketplace/RecommendationEngine.hpp"
#include "runtime/GameLoader.hpp"
#include "runtime/GameLoop.hpp"

namespace engine::runtime {

namespace {
// Kronos ("In-App Auto-Updater"): the real repository the real update
// check queries -- the same one the Bootstrap Installer pulls its own
// real release archives from (see installer/src/main.cpp).
// Kronos backend base URL. Overridable via the environment so a real
// developer can point the launcher at a local service without editing
// and rebuilding -- and so a self-hosted deployment is a config change
// rather than a fork.
// Small, self-contained percent-encoder for the two query values the
// browser sign-in URL carries. Deliberately not pulling libcurl into
// this translation unit for ten lines of RFC 3986.
std::string urlEncodeComponent(const std::string& value) {
    static const char* kHex = "0123456789ABCDEF";
    std::string out;
    out.reserve(value.size() * 3);
    for (unsigned char c : value) {
        bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
                          c == '_' || c == '.' || c == '~';
        if (unreserved) {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += kHex[(c >> 4) & 0xF];
            out += kHex[c & 0xF];
        }
    }
    return out;
}

const core::KronosClientConfig& kronosClientConfig() {
    // Resolved once, on first use: config.json > environment > default.
    static const core::KronosClientConfig config = [] {
        core::KronosClientConfig loaded = core::loadKronosClientConfig(core::executableDirectory());
        core::logInfo("Kronos", "backend %s (from %s)", loaded.apiUrl.c_str(), loaded.source.c_str());
        return loaded;
    }();
    return config;
}

// Where the browser sign-in page lives. Overridable so a self-hosted or
// local deployment is configuration rather than a rebuild.
std::string resolveKronosAuthUrl() {
    return kronosClientConfig().authUrl;
}


std::string resolveKronosBackendUrl() {
    return kronosClientConfig().apiUrl;
}

// Kronos Client shell chrome geometry -- one definition, used by both
// the chrome itself and every content panel that has to inset around it.
constexpr float kSidebarWidth = 214.0f;
constexpr float kTopBarHeight = 64.0f;
constexpr float kBrandPanelWidth = 300.0f;

constexpr const char* kUpdateRepoOwner = "Bands-yt";
constexpr const char* kUpdateRepoName = "kronos-platform";

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
        // Kronos ("via join tickets"): actionable rather than alarming --
        // the overwhelmingly common cause is simply an expired ticket
        // (they last 60 seconds), not anything wrong with the account.
        case net::JoinFailureReason::InvalidTicket:
            return "Your join pass expired or was not accepted. Press Play again to get a new one.";
        case net::JoinFailureReason::ServerError:
            return "That server could not start a character for you. Try again, or pick another server.";
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
      spawnOfflinePlayerEntity_(std::move(spawnOfflinePlayerEntity)),
      kronosApi_(resolveKronosBackendUrl()) {}

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
        // The avatar preview moved from Home to the Avatar tab, but this
        // gate still said Home -- so the offscreen target was never
        // rendered there and the viewport showed an empty grey box. The
        // ImGui::Image() binding was fine all along; nothing was drawing
        // into it.
        if (avatarPreviewVisible() && homeAvatarPreview_) {
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

    // Kronos ("In-App Auto-Updater" -- "On startup ... query the GitHub
    // Releases API"): fired once here, on a real background thread, so
    // the real network round-trip never delays the first frame. Its
    // result surfaces on the Home screen whenever it lands.
    startUpdateCheck();

    // Kronos ("Backend integration"): restore a previously saved Kronos
    // session from the OS credential store, so a returning player is
    // already signed in without another password prompt. Silent if there
    // is nothing saved.
    //
    // Kronos ("kronos:// launch URI" hand-off): EXCEPT when this launch
    // carried a real hand-off code -- that takes priority over whatever
    // native session happens to already be persisted (the user just
    // explicitly authenticated in the browser and clicked a specific
    // button; silently signing them in as a possibly-different,
    // possibly-stale account instead would be a real, confusing bug, not
    // a graceful fallback). A successful exchange persists its own
    // refresh token exactly like any other sign-in, so it naturally
    // becomes what future plain launches restore.
    if (!pendingDeepLinkHandoffCode_.empty()) {
        startHandoffExchange(std::move(pendingDeepLinkHandoffCode_));
    } else {
        startBackendSessionRestore();
    }
    // Load the public catalogue immediately -- Discover should have real
    // content on first paint rather than only after signing in.
    startCatalogueFetch();

    return true;
}

void RuntimeShell::shutdown() {
    // Kronos ("Google OAuth Authentication"): real, joins a still-in-
    // flight background sign-in before this object's own members (that
    // thread's lambda captures `this`) are destroyed. A real, bounded
    // wait -- googleSignIn() itself has its own real timeout, so this
    // can't hang forever, just until that real timeout (or a real
    // completed sign-in) elapses.
    if (googleSignInThread_.joinable()) googleSignInThread_.join();
    // Same real reason as the sign-in thread above: this one's lambda
    // captures `this` too, so it must not outlive these members.
    if (updateCheckThread_.joinable()) updateCheckThread_.join();
    // Same reason again: each of these lambdas captures `this`.
    if (backendAuthThread_.joinable()) backendAuthThread_.join();
    if (catalogueFetchThread_.joinable()) catalogueFetchThread_.join();
    if (allocationThread_.joinable()) allocationThread_.join();
    if (friendsThread_.joinable()) friendsThread_.join();
    if (friendSearchThread_.joinable()) friendSearchThread_.join();
    if (presenceThread_.joinable()) presenceThread_.join();
    if (directoryThread_.joinable()) directoryThread_.join();
    if (avatarConfigPushThread_.joinable()) avatarConfigPushThread_.join();
    if (avatarConfigPullThread_.joinable()) avatarConfigPullThread_.join();

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

// One definition of "is the avatar preview on screen right now", used by
// both the render pass and the per-frame update so they can never
// disagree about whether to draw it.
bool RuntimeShell::avatarPreviewVisible() const {
    if (showSplash_) return false;
    return state_ == ShellState::AvatarShop;
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
    // Kronos ("Esc Pause Menu" redesign): the player list moved from its
    // own standalone window into a real tab inside the pause menu (see
    // drawPlayerListOverlay()'s own comment) -- this real, scripted
    // ui.showPlayerList() entry point (ScriptUiApi.cpp) now opens that
    // menu with the Players tab pre-selected instead. showPlayerListOverlay_
    // is reused here as the real one-shot "select this tab" signal
    // drawPlayerListOverlay() consumes and clears the next time it draws.
    showPauseMenuOverlay_ = true;
    showPlayerListOverlay_ = true;
    app_.setMovementInputSuspended(true);
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
    showPauseMenuOverlay_ = false;
    // A stale server_key from a session that just ended must never
    // survive into the next presence heartbeat -- see these members'
    // own comment on RuntimeShell.hpp.
    onlineSessionGameId_.clear();
    onlineSessionServerKey_.clear();

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
    // Deliberately NOT gated on state_: the sidebar sets state_ to
    // GameCatalogue before calling this, and the old `state_ != Home`
    // guard then skipped the disk scan entirely -- which is why the
    // Create tab came up empty. Scanning local projects is also
    // completely independent of backend connectivity: local games must
    // list and launch with no network at all.
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

    // Kronos ("Shift Lock" mouse-lock toggle -- live-reported issue: the
    // cursor used to be captured/hidden the instant gameplay started,
    // with no way to see or use it short of leaving): real, honest
    // default now -- mouse free (not captured), matching what the player
    // sees immediately; Shift (see tick()'s own toggle handling) is the
    // one, real way to opt into the old always-captured look-around
    // behavior. mouseLockEnabled_ itself is left untouched here (not
    // forced false) so a player's own Shift-lock preference from earlier
    // in this same session -- e.g. leaving one game and starting another
    // -- persists across that transition rather than silently resetting.
    app_.input().setRelativeMouseMode(mouseLockEnabled_);
    state_ = computeNextState(state_, ShellEvent::GameLoadFinished);
}

void RuntimeShell::launchStudio() {
    studioLaunchError_.clear();
    std::string studioPath = core::executableDirectory() + "/studio";
    if (!core::launchProcess(studioPath, {})) {
        // Surfaced in the Create tab, not just stderr: a button that
        // silently does nothing is worse than one that says why.
        studioLaunchError_ = "Could not start Kronos Studio (expected it next to this executable at \"" +
                             studioPath + "\").";
        std::fprintf(stderr, "RuntimeShell: %s\n", studioLaunchError_.c_str());
        return;
    }
    notify(core::NotificationKind::SystemMessage, "Kronos Studio", "Studio is starting.");
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
    pollGoogleSignInResult();
    pollUpdateCheckResult();
    pollBackendResults();

    // Debounced search fire + periodic friends/presence refresh. 15s
    // matches the heartbeat the backend expects, so a friend's status
    // never lags much more than one beat behind reality.
    if (friendSearchDebounce_ > 0.0f) {
        friendSearchDebounce_ -= dt;
        if (friendSearchDebounce_ <= 0.0f) {
            friendSearchDebounce_ = 0.0f;
            startFriendSearch(std::string(friendSearchBuffer_));
        }
    }
    if (kronosApi_.isSignedIn()) {
        friendsRefreshTimer_ -= dt;
        if (friendsRefreshTimer_ <= 0.0f) {
            friendsRefreshTimer_ = 15.0f;
            startFriendsFetch();
            presenceHeartbeat();
        }
    }

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
            // Kronos ("Shift Lock" mouse-lock toggle): same real default
            // as finishPendingGameLoad()'s own local-Play entry point --
            // see that call's own comment.
            app_.input().setRelativeMouseMode(mouseLockEnabled_);
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

    // Kronos ("Esc Pause Menu" fix -- live-reported issue: Escape
    // instantly disconnected the player instead of opening a menu, with
    // no confirmation and no way to back out of an accidental press).
    // Escape now toggles the real pause-menu overlay (player list,
    // graphics settings, notifications, report, and an explicit "Leave
    // Session" button -- see drawPlayerListOverlay()'s own comment for
    // that panel) instead of leaving directly. The real reachability
    // guarantee the previous "Active Joining UI" fix cared about --
    // there must always be a way back to Home even while relative mouse
    // mode has the cursor captured/hidden, so the panel's own mouse-driven
    // buttons are unreachable -- still holds: this is a keyboard toggle,
    // not a mouse click, so it works identically whether or not the
    // cursor is currently captured. Movement input is suspended exactly
    // while the menu is open, same convention every other overlay here
    // (Settings/Friends/Notifications/Chat) already follows, so the
    // player's character doesn't keep walking while they're menuing.
    // Kronos ("Player & Chat System" -- chat panel): Escape closes an
    // open chat box first, same real "Escape backs out one real layer at
    // a time" convention every other real menu in this shell already
    // follows (SessionBrowser/GameCatalogue/Error's own ReturnHome) --
    // without this real guard, pressing Escape to cancel a half-typed
    // chat message would instead pop the pause menu open underneath it,
    // a real, jarring bug this check exists specifically to prevent.
    bool escapeDown = state_ == ShellState::InGame && !showChatPanel_ && app_.input().isActionDown("ToggleMenu");
    if (escapeDown && !escapeKeyWasDown_) {
        showPauseMenuOverlay_ = !showPauseMenuOverlay_;
        app_.setMovementInputSuspended(showPauseMenuOverlay_);
        // Kronos ("Shift Lock" mouse-lock toggle -- live-reported issue:
        // "my cursor is stuck in the middle of the screen" -- the pause
        // menu drew real, clickable buttons while relative mouse mode
        // kept the real OS cursor captured/hidden/recentered, so none of
        // them were actually reachable): the menu always forces the real
        // cursor free while it's open, regardless of the player's own
        // mouseLockEnabled_ preference, then restores exactly that
        // preference on close -- a player who had Shift-locked look-
        // around on before opening the menu gets it back after closing,
        // one who didn't stays free.
        app_.input().setRelativeMouseMode(showPauseMenuOverlay_ ? false : mouseLockEnabled_);
    }
    escapeKeyWasDown_ = escapeDown;

    // Kronos ("Shift Lock" mouse-lock toggle): real, player-controlled --
    // Shift both drives the existing real "Run" action
    // (CharacterController::configureInput(), continuous -- "is it down
    // right now", real walk/run speed) and, on its own press-edge here,
    // toggles this real preference (discrete -- "was it just pressed") --
    // the two read the same physical key without conflicting, the same
    // way a single key drives both "hold to run" and "tap to toggle" in
    // many real games. Gated off while the pause menu/chat panel is open
    // (or any real ImGui text field wants keyboard input) so it can't
    // fire while the player is typing a chat message or a report
    // description.
    ImGuiIO& shiftLockIo = ImGui::GetIO();
    bool mouseLockKeyDown = state_ == ShellState::InGame && !showPauseMenuOverlay_ && !showChatPanel_ &&
                             !shiftLockIo.WantTextInput && app_.input().isActionDown("Run");
    if (mouseLockKeyDown && !mouseLockKeyWasDown_) {
        mouseLockEnabled_ = !mouseLockEnabled_;
        app_.input().setRelativeMouseMode(mouseLockEnabled_);
    }
    mouseLockKeyWasDown_ = mouseLockKeyDown;

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
    // Same gate as the render pass above, for the same reason -- without
    // this the preview would render but never animate or accept orbit
    // input on the Avatar tab.
    if (avatarPreviewVisible() && homeAvatarPreview_) homeAvatarPreview_->update(dt);

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
        // Kronos Client shell chrome -- drawn around every browsing state.
        // Deliberately NOT around Loading/InGame/Error: those are
        // full-screen moments where a sidebar and a sign-in button would
        // be noise, not navigation.
        bool showChrome = state_ == ShellState::Home || state_ == ShellState::GameCatalogue ||
                          state_ == ShellState::AvatarShop || state_ == ShellState::Settings ||
                          state_ == ShellState::Friends || state_ == ShellState::Notifications ||
                          state_ == ShellState::SessionBrowser;
        if (showChrome) {
            drawSidebar();
            drawTopBar();
            drawBrandPanel();
        }

        switch (state_) {
            case ShellState::Home: drawHomePanel(); break;
            case ShellState::SessionBrowser: drawSessionBrowserPanel(); break;
            case ShellState::Loading: drawLoadingPanel(); break;
            case ShellState::GameCatalogue: drawGameCataloguePanel(); break;
            case ShellState::AvatarShop: drawAvatarShopPanel(); break;
            case ShellState::Settings: drawSettingsPanel(); break;
            // Kronos ("Live Dashboard View"): the Friends slot now hosts
            // the account directory, which is the panel this state is
            // actually reachable from in the sidebar.
            case ShellState::Friends: drawDirectoryPanel(); break;
            case ShellState::Notifications: drawNotificationsPanel(); break;
            case ShellState::Error: drawErrorPanel(); break;
            case ShellState::InGame:
                tickTrailerCaptureMode(dt);
                tickEmoteActivation();
                if (!trailerHudHidden_) {
                    // Kronos ("Shift Lock" mouse-lock toggle): real,
                    // minimal crosshair -- the one, real visual cue that
                    // the cursor is currently captured (mouse movement
                    // drives camera look) versus free (normal pointer,
                    // see mouseLockEnabled_'s own comment). Screen-space,
                    // drawn via the foreground draw list so it always
                    // sits above the 3D scene and every real ImGui panel.
                    if (mouseLockEnabled_) {
                        ImVec2 center(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f);
                        ImDrawList* fg = ImGui::GetForegroundDrawList();
                        ImU32 crosshairColor = IM_COL32(255, 255, 255, 200);
                        fg->AddLine(ImVec2(center.x - 6.0f, center.y), ImVec2(center.x + 6.0f, center.y), crosshairColor, 1.5f);
                        fg->AddLine(ImVec2(center.x, center.y - 6.0f), ImVec2(center.x, center.y + 6.0f), crosshairColor, 1.5f);
                    }
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
// Kronos ("Warm Ivory & Playful Sunset"): real, shared -- previously
// duplicated as local consts inside drawHomePanel() for its own two
// primary-action buttons (Game Catalogue/Launch Studio). A real,
// reusable helper now, so the same primary-action coral applies
// consistently to other real primary actions elsewhere in engine_runtime
// (Game Catalogue's own "Play"/live-session "Join" buttons, see
// drawGameCard() below) instead of drifting out of sync with a second,
// separately-hand-copied color triple. Matches core::applyKronosUITheme()'s
// own kAccent exactly (#DD6B20) -- was green before the theme's own
// accent color changed; kept in sync here rather than left stale.
// Kronos Client spec: primary actions (Play, Sign In) are Vibrant Green
// #00B259, deliberately distinct from the sky-blue accent that marks
// selection/active state -- see UITheme.cpp's own note on why one colour
// for both would make a selected tab look like a button.
ImVec4 paletteColor(const float (&rgba)[4]) { return ImVec4(rgba[0], rgba[1], rgba[2], rgba[3]); }

void pushPrimaryActionButtonColors() {
    using namespace engine::core::kronos_palette;
    ImGui::PushStyleColor(ImGuiCol_Button, paletteColor(kGreen));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, paletteColor(kGreenHover));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, paletteColor(kGreenActive));
}
void popPrimaryActionButtonColors() { ImGui::PopStyleColor(3); }

// Kronos: the smooth, fixed-shape Kronos character head, drawn
// procedurally. Matches the reference silhouette (rounded head, flat
// rounded jaw, the two side "port" discs) closely enough to read as the
// same character at profile-icon size, without needing a portrait asset
// per user -- and, unlike a downloaded image, it is always available
// offline and costs no GPU texture.
void drawAvatarHeadGlyph(ImDrawList* drawList, ImVec2 center, float radius) {
    using namespace engine::core::kronos_palette;
    const ImU32 skin = ImGui::GetColorU32(ImVec4(0.93f, 0.87f, 0.80f, 1.0f));
    const ImU32 feature = ImGui::GetColorU32(ImVec4(0.13f, 0.13f, 0.14f, 1.0f));
    const ImU32 port = ImGui::GetColorU32(ImVec4(0.24f, 0.25f, 0.27f, 1.0f));

    // Head: a rounded rectangle rather than a circle -- the reference
    // shape is squarer at the jaw than a plain ball.
    ImVec2 topLeft(center.x - radius * 0.86f, center.y - radius);
    ImVec2 bottomRight(center.x + radius * 0.86f, center.y + radius * 0.92f);
    drawList->AddRectFilled(topLeft, bottomRight, skin, radius * 0.52f);

    // Side ports.
    drawList->AddCircleFilled(ImVec2(topLeft.x + radius * 0.04f, center.y), radius * 0.20f, port, 16);
    drawList->AddCircleFilled(ImVec2(bottomRight.x - radius * 0.04f, center.y), radius * 0.20f, port, 16);

    // Eyes and mouth, scaled off the radius so they stay proportional at
    // any icon size.
    float eyeY = center.y - radius * 0.14f;
    drawList->AddCircleFilled(ImVec2(center.x - radius * 0.32f, eyeY), radius * 0.13f, feature, 12);
    drawList->AddCircleFilled(ImVec2(center.x + radius * 0.32f, eyeY), radius * 0.13f, feature, 12);
    drawList->AddRectFilled(ImVec2(center.x - radius * 0.24f, center.y + radius * 0.30f),
                             ImVec2(center.x + radius * 0.24f, center.y + radius * 0.40f), feature, radius * 0.05f);
}

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

// Kronos Client: the Home canvas.
//
// This replaces the legacy full-viewport main menu wholesale. That layout
// ("Alpha Platform", a raw Player text field, a Google button, and a grid
// of Launch Studio / Avatar Shop / Friends buttons) plus its separate
// floating "Your Avatar" window predated the shell chrome, and once the
// chrome existed the two drew straight through each other. Navigation now
// lives in the sidebar and identity in the top bar, so none of it is
// re-created here -- it is deleted, not hidden.
// Every content view opens through these two, so the canvas geometry
// lives in ONE place instead of each panel re-deriving the inset and
// drifting. Also forces the charcoal background, so no raw ImGui grey
// bleeds through anywhere.
void RuntimeShell::beginContentCanvas(const char* id) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + kSidebarWidth, viewport->WorkPos.y + kTopBarHeight));
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x - kSidebarWidth - kBrandPanelWidth,
                                     viewport->WorkSize.y - kTopBarHeight));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, paletteColor(core::kronos_palette::kCharcoal));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20.0f, 18.0f));
    ImGui::Begin(id, nullptr,
                  ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);
}

void RuntimeShell::endContentCanvas() {
    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

void RuntimeShell::drawHomePanel() {
    ensureLocalProfileLoaded();
    using namespace core::kronos_palette;

    beginContentCanvas("Home");

    // --- guest banner -------------------------------------------------
    std::optional<core::KronosUser> user = kronosApi_.currentUser();
    bool isGuest = user.has_value() && user->email.empty() && user->displayName.rfind("Guest", 0) == 0;
    if (isGuest) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(kSkyBlue[0] * 0.22f, kSkyBlue[1] * 0.22f, kSkyBlue[2] * 0.28f, 1.0f));
        ImGui::BeginChild("##guest_banner", ImVec2(0.0f, 46.0f), true);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(paletteColor(kTextBright),
                            "Playing as Guest -- Sign Up to save progress and add friends!");
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 96.0f);
        pushPrimaryActionButtonColors();
        if (ImGui::Button("Sign Up", ImVec2(96.0f, 26.0f))) startBrowserSignIn();
        popPrimaryActionButtonColors();
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0.0f, 12.0f));
    }

    // --- update banner ------------------------------------------------
    if (updateAvailable_ && !updateBannerDismissed_) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, paletteColor(kSlate));
        ImGui::BeginChild("##update_banner", ImVec2(0.0f, 68.0f), true);
        ImGui::TextColored(paletteColor(kSkyBlue), "Kronos %s is available", updateAvailableTag_.c_str());
        ImGui::TextColored(paletteColor(kTextMuted), "You're running %s.", core::kKronosVersion);
        pushPrimaryActionButtonColors();
        if (ImGui::SmallButton("Update now")) {
            if (!startUpdateDownload()) {
                notify(core::NotificationKind::SystemMessage, "Update failed to start", updateStatusMessage_);
            }
        }
        popPrimaryActionButtonColors();
        ImGui::SameLine();
        if (ImGui::SmallButton("Later")) updateBannerDismissed_ = true;
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0.0f, 12.0f));
    }

    drawFriendsCarousel();

    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    ImGui::SeparatorText("Jump back in");
    ImGui::TextColored(paletteColor(kTextMuted),
                       "Browse published games under Discover, or open your own local projects under Create.");

    if (backendReachability_ == BackendReachability::Unreachable) {
        ImGui::Dummy(ImVec2(0.0f, 8.0f));
        ImGui::TextColored(paletteColor(kTextMuted),
                           "Kronos services are unreachable right now. Local / Dev games under Create still work.");
    }

    endContentCanvas();

    // Modals are drawn outside the canvas so they centre on the whole
    // viewport rather than inside the content inset.
    drawAddFriendsModal();
}

// Kronos ("Home Screen Friends Carousel"): the circular Add-Friends action
// card followed by circular friend badges with live status. Everything
// here comes from a real /v1/friends/list response -- there is no
// placeholder friend, so an empty carousel means you genuinely have none.
// Kronos ("Direct Join"): connects straight into the server a friend is
// actually on, using the join ticket the backend minted for THIS user and
// THAT server. Nothing here trusts the client's own idea of where the
// friend is -- the ticket is what the game server validates.
void RuntimeShell::joinFriendGame(const core::KronosFriend& friendEntry) {
    if (friendEntry.joinTicket.empty() || friendEntry.currentServerId.empty()) {
        friendsStatusMessage_ = "That friend is no longer in a joinable game.";
        return;
    }

    // The friends list carries a ticket but not a host/port -- allocation
    // is what resolves an address. Ask for one against the game the friend
    // is in, which also re-checks capacity as of right now.
    if (friendEntry.currentGameId.empty()) {
        friendsStatusMessage_ = "Kronos did not report which game your friend is in.";
        return;
    }
    // friendEntry.currentGameId is the real numeric games.id (this is
    // what presenceHeartbeat() now reports, matching /v1/sessions/
    // allocate's own canonical id) -- but startServerAllocation() (and
    // the backend's /v1/sessions/allocate it calls) takes a SLUG, not a
    // numeric id. Resolved against the catalogue already in memory
    // rather than guessing the two are interchangeable.
    for (const core::CatalogueGame& game : onlineGames_) {
        if (game.id == friendEntry.currentGameId) {
            startServerAllocation(game.slug, friendEntry.username.empty() ? friendEntry.displayName
                                                                           : friendEntry.username);
            return;
        }
    }
    friendsStatusMessage_ = "That friend's game isn't in your current catalogue yet -- try refreshing.";
}

// Real 15s presence heartbeat. Fire-and-forget on a detached-style
// worker so a slow network never stalls a frame; a dropped beat only
// means friends see this user go offline slightly early.
void RuntimeShell::presenceHeartbeat() {
    if (presenceThread_.joinable()) presenceThread_.join();
    if (directoryThread_.joinable()) directoryThread_.join();
    bool inGame = state_ == ShellState::InGame;
    // Kronos ("Join Friend pathway"): real game id/server_key, read on
    // this thread (the caller's) before handing off to the background
    // one -- same "snapshot on the calling thread, pass by value" rule
    // every other background call here already follows. Empty when this
    // InGame session isn't a real online allocation (e.g. a purely
    // local play session) -- presenceHeartbeat() has nothing honest to
    // report for those, same as being online_launcher.
    std::string gameId = inGame ? onlineSessionGameId_ : std::string();
    std::string serverKey = inGame ? onlineSessionServerKey_ : std::string();
    presenceThread_ = std::thread([this, inGame, gameId, serverKey]() {
        kronosApi_.sendPresenceHeartbeat(inGame ? "in_game" : "online_launcher", gameId, serverKey);
    });
}

// GET /v1/avatar/me, applied over local state once it lands (see
// pollBackendResults()'s own "avatar config pull" block) -- signed-in
// only; a signed-out player has no backend copy to pull, and keeps
// whatever local_avatar_loadout.loadout already has.
void RuntimeShell::pullAvatarConfigFromBackend() {
    if (!kronosApi_.isSignedIn()) return;
    if (avatarConfigPullThread_.joinable()) avatarConfigPullThread_.join();
    avatarConfigPullThread_ = std::thread([this]() {
        core::AvatarConfig result = kronosApi_.fetchAvatarConfig();
        std::lock_guard<std::mutex> lock(avatarConfigPullMutex_);
        avatarConfigPullPendingResult_ = std::move(result);
    });
}

// PUT /v1/avatar/me. Fire-and-forget, same reasoning as
// presenceHeartbeat() above: a dropped save just means the backend's
// copy is a little stale until the next successful equip/unequip, not
// a real failure the player needs to see. Real, honest no-op when
// signed out -- there is no account to save against.
void RuntimeShell::pushAvatarConfigToBackend() {
    if (!kronosApi_.isSignedIn()) return;
    core::AvatarConfig config;
    config.skinToneIndex = localProfile_.skinToneIndex;
    config.headShapeIndex = localProfile_.headShapeIndex;
    config.bodyHeight = localProfile_.bodyHeight;
    config.bodyWidth = localProfile_.bodyWidth;
    config.bodyLimbScale = localProfile_.bodyLimbScale;
    config.bodyTorsoLength = localProfile_.bodyTorsoLength;
    config.bodyShoulderWidth = localProfile_.bodyShoulderWidth;
    config.clothingFitIndex = localProfile_.clothingFitIndex;
    for (const auto& [category, itemId] : avatarLoadout_.equippedItems()) {
        config.equippedItems[core::avatarItemCategoryName(category)] = itemId;
    }
    if (avatarConfigPushThread_.joinable()) avatarConfigPushThread_.join();
    avatarConfigPushThread_ = std::thread([this, config]() { (void)kronosApi_.saveAvatarConfig(config); });
}

void RuntimeShell::startDirectoryFetch(bool loadNextPage) {
    if (directoryFetchInProgress_.load() || !kronosApi_.isSignedIn()) return;
    if (loadNextPage && directoryNextCursor_.empty()) return;
    if (directoryThread_.joinable()) directoryThread_.join();

    directoryFetchInProgress_.store(true);
    directoryAppendingPage_ = loadNextPage;
    const std::string cursor = loadNextPage ? directoryNextCursor_ : std::string();

    directoryThread_ = std::thread([this, cursor]() {
        core::DirectoryResult page = kronosApi_.fetchUserDirectory(kCatalogueBatchSize, cursor);
        core::PresenceSummary summary = kronosApi_.fetchPresenceSummary();
        std::lock_guard<std::mutex> lock(directoryMutex_);
        directoryPendingResult_ = std::move(page);
        directoryPendingSummary_ = summary;
        directoryFetchInProgress_.store(false);
    });
}

// Kronos ("Live Dashboard View"): the account directory, with the spec's
// four status colours. Every row comes from a real /v1/users response --
// there is no synthesised account, so an empty directory means the
// directory really is empty.
void RuntimeShell::drawDirectoryPanel() {
    using namespace core::kronos_palette;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + kSidebarWidth, viewport->WorkPos.y + kTopBarHeight));
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x - kSidebarWidth - kBrandPanelWidth,
                                     viewport->WorkSize.y - kTopBarHeight));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, paletteColor(kCharcoal));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20.0f, 18.0f));
    ImGui::Begin("Directory", nullptr,
                  ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);

    ImGui::TextColored(paletteColor(kTextBright), "Accounts");

    if (!kronosApi_.isSignedIn()) {
        ImGui::TextColored(paletteColor(kTextMuted), "Sign in to view the Kronos account directory.");
        ImGui::End();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        return;
    }

    // --- summary tiles -------------------------------------------------
    if (directorySummary_.success && directorySummary_.available) {
        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        auto tile = [&](const char* label, int value, const ImVec4& color) {
            ImGui::BeginChild(label, ImVec2(150.0f, 58.0f), true);
            ImGui::TextColored(color, "%d", value);
            ImGui::TextColored(paletteColor(kTextMuted), "%s", label);
            ImGui::EndChild();
            ImGui::SameLine();
        };
        tile("Online", directorySummary_.totalOnline, paletteColor(kGreen));
        tile("In Studio", directorySummary_.inStudio, ImVec4(0.90f, 0.55f, 0.15f, 1.0f));
        tile("Playing", directorySummary_.inGame, paletteColor(kSkyBlue));
        tile("Registered", directorySummary_.registeredAccounts, paletteColor(kTextBright));
        ImGui::NewLine();
    } else if (directorySummary_.success) {
        // The backend told us presence is unavailable. Saying so beats
        // drawing a confident row of zeroes.
        ImGui::TextColored(paletteColor(kTextMuted), "Live presence is unavailable right now.");
    }

    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 90.0f);
    ImGui::BeginDisabled(directoryFetchInProgress_.load());
    if (ImGui::SmallButton("Refresh##directory")) {
        directoryNextCursor_.clear();
        startDirectoryFetch(/*loadNextPage=*/false);
    }
    ImGui::EndDisabled();

    if (!directoryStatusMessage_.empty()) {
        ImGui::TextColored(ImVec4(0.85f, 0.35f, 0.30f, 1.0f), "%s", directoryStatusMessage_.c_str());
    }

    ImGui::Separator();
    ImGui::BeginChild("##directory_rows", ImVec2(0.0f, 0.0f), false);

    if (directoryUsers_.empty() && !directoryFetchInProgress_.load()) {
        ImGui::TextColored(paletteColor(kTextMuted), "No accounts to show yet.");
    }

    for (const core::DirectoryUser& entry : directoryUsers_) {
        ImGui::PushID(entry.id.c_str());
        ImGui::BeginChild("##row", ImVec2(0.0f, 56.0f), true);

        // Spec colours: grey offline, green launcher, orange studio, blue
        // playing.
        ImVec4 statusColor = ImVec4(0.45f, 0.46f, 0.48f, 1.0f);
        const char* statusLabel = "Offline";
        if (entry.status == "online_launcher") {
            statusColor = paletteColor(kGreen);
            statusLabel = "Online";
        } else if (entry.status == "in_studio") {
            statusColor = ImVec4(0.90f, 0.55f, 0.15f, 1.0f);
            statusLabel = "In Studio";
        } else if (entry.status == "in_game") {
            statusColor = paletteColor(kSkyBlue);
            statusLabel = "Playing";
        }

        ImVec2 origin = ImGui::GetCursorScreenPos();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddCircleFilled(ImVec2(origin.x + 10.0f, origin.y + 16.0f), 6.0f,
                                   ImGui::GetColorU32(statusColor), 16);
        drawAvatarHeadGlyph(drawList, ImVec2(origin.x + 42.0f, origin.y + 18.0f), 16.0f);
        ImGui::Dummy(ImVec2(66.0f, 36.0f));
        ImGui::SameLine();

        ImGui::BeginGroup();
        ImGui::TextColored(paletteColor(kTextBright), "%s", entry.directoryName.c_str());
        ImGui::TextColored(statusColor, "%s", statusLabel);
        if (!entry.hasUsername) {
            // Visible, but marked: this account has not picked a handle
            // yet, which is worth showing rather than silently implying
            // the display name is one.
            ImGui::SameLine();
            ImGui::TextColored(paletteColor(kTextMuted), "  (no handle yet)");
        }
        ImGui::EndGroup();

        ImGui::EndChild();
        ImGui::PopID();
    }

    // Same infinite-scroll trigger the Discover grid uses.
    if (!directoryNextCursor_.empty() && !directoryFetchInProgress_.load()) {
        const float scrollY = ImGui::GetScrollY();
        const float scrollMax = ImGui::GetScrollMaxY();
        if (scrollMax > 0.0f && scrollY >= scrollMax - ImGui::GetWindowHeight()) {
            startDirectoryFetch(/*loadNextPage=*/true);
        }
    }
    if (directoryFetchInProgress_.load()) {
        ImGui::TextColored(paletteColor(kTextMuted), "Loading...");
    } else if (directoryNextCursor_.empty() && !directoryUsers_.empty()) {
        ImGui::TextColored(paletteColor(kTextMuted), "%zu account(s).", directoryUsers_.size());
    }

    ImGui::EndChild();
    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

void RuntimeShell::startFriendsFetch() {
    if (friendsFetchInProgress_.load() || !kronosApi_.isSignedIn()) return;
    if (friendsThread_.joinable()) friendsThread_.join();
    friendsFetchInProgress_.store(true);
    friendsThread_ = std::thread([this]() {
        core::FriendsResult result = kronosApi_.fetchFriends();
        std::lock_guard<std::mutex> lock(friendsMutex_);
        friendsPendingResult_ = std::move(result);
        friendsFetchInProgress_.store(false);
    });
}

void RuntimeShell::startFriendSearch(const std::string& queryText) {
    if (friendSearchInProgress_.load()) return;
    if (friendSearchThread_.joinable()) friendSearchThread_.join();
    friendSearchInProgress_.store(true);
    friendSearchStatus_ = "Searching...";
    friendSearchThread_ = std::thread([this, queryText]() {
        core::UserSearchResponse result = kronosApi_.searchUsers(queryText);
        std::lock_guard<std::mutex> lock(friendSearchMutex_);
        friendSearchPendingResult_ = std::move(result);
        friendSearchInProgress_.store(false);
    });
}

void RuntimeShell::startFriendRequest(const std::string& userId) {
    if (friendSearchThread_.joinable()) friendSearchThread_.join();
    friendSearchThread_ = std::thread([this, userId]() {
        std::string error;
        bool ok = kronosApi_.sendFriendRequest(userId, error);
        std::lock_guard<std::mutex> lock(friendSearchMutex_);
        // Reflected optimistically in the row below; this only reports a
        // real failure, so a rejected request never looks like it worked.
        if (!ok) friendSearchStatus_ = error;
    });
    // Optimistic local state: the row flips to "Pending" immediately
    // rather than after a round trip, and is corrected by the next real
    // search if the server disagreed.
    for (core::UserSearchResult& entry : friendSearchResults_) {
        if (entry.id == userId) entry.relationship = "request_sent";
    }
}

// Kronos ("Add Friends modal"): username search against
// /v1/users/search, with real per-row relationship state so a row shows
// Add / Pending / Friends truthfully rather than always offering "Add".
void RuntimeShell::drawAddFriendsModal() {
    using namespace core::kronos_palette;

    if (showGuestUpgradePrompt_) {
        ImGui::OpenPopup("Create a free account");
        showGuestUpgradePrompt_ = false;
    }
    if (ImGui::BeginPopupModal("Create a free account", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextColored(paletteColor(kTextBright), "Create a free account to add friends and join their games.");
        ImGui::TextColored(paletteColor(kTextMuted), "Your local projects stay exactly where they are.");
        ImGui::Dummy(ImVec2(0.0f, 8.0f));
        pushPrimaryActionButtonColors();
        if (ImGui::Button("Sign Up", ImVec2(140.0f, 32.0f))) {
            ImGui::CloseCurrentPopup();
            startBrowserSignIn();
        }
        popPrimaryActionButtonColors();
        ImGui::SameLine();
        if (ImGui::Button("Not now", ImVec2(110.0f, 32.0f))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (showAddFriendsModal_) {
        ImGui::OpenPopup("Add Friends");
        showAddFriendsModal_ = false;
    }

    ImGui::SetNextWindowSize(ImVec2(460.0f, 460.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Add Friends", nullptr, ImGuiWindowFlags_NoSavedSettings)) return;

    ImGui::TextColored(paletteColor(kTextMuted), "Search for someone by username.");
    ImGui::SetNextItemWidth(-1.0f);
    bool changed = ImGui::InputTextWithHint("##friend_search", "Username", friendSearchBuffer_,
                                             sizeof(friendSearchBuffer_));

    std::string queryText = friendSearchBuffer_;
    if (changed) {
        friendSearchStatus_.clear();
        if (queryText.size() >= 3) {
            // Debounced by a timer rather than firing on every keystroke,
            // which would hammer the endpoint (and its rate limit) with
            // queries the user never finished typing.
            friendSearchDebounce_ = 0.35f;
        } else {
            friendSearchResults_.clear();
            friendSearchDebounce_ = 0.0f;
            if (!queryText.empty()) friendSearchStatus_ = "Enter at least 3 characters.";
        }
    }

    if (friendSearchInProgress_.load()) {
        ImGui::TextColored(paletteColor(kTextMuted), "Searching...");
    } else if (!friendSearchStatus_.empty()) {
        ImGui::TextColored(ImVec4(0.85f, 0.35f, 0.30f, 1.0f), "%s", friendSearchStatus_.c_str());
    }

    ImGui::Separator();
    ImGui::BeginChild("##friend_search_results", ImVec2(0.0f, 300.0f), false);
    if (friendSearchResults_.empty() && !friendSearchInProgress_.load() && queryText.size() >= 3 &&
        friendSearchStatus_.empty()) {
        ImGui::TextColored(paletteColor(kTextMuted), "No players found.");
    }

    for (const core::UserSearchResult& entry : friendSearchResults_) {
        ImGui::PushID(entry.id.c_str());
        ImGui::BeginChild("##row", ImVec2(0.0f, 60.0f), true);

        ImVec2 origin = ImGui::GetCursorScreenPos();
        drawAvatarHeadGlyph(ImGui::GetWindowDrawList(), ImVec2(origin.x + 22.0f, origin.y + 20.0f), 18.0f);
        ImGui::Dummy(ImVec2(48.0f, 40.0f));
        ImGui::SameLine();

        ImGui::BeginGroup();
        ImGui::TextColored(paletteColor(kTextBright), "%s",
                            entry.username.empty() ? entry.displayName.c_str() : entry.username.c_str());
        if (!entry.displayName.empty() && !entry.username.empty() && entry.displayName != entry.username) {
            ImGui::TextColored(paletteColor(kTextMuted), "%s", entry.displayName.c_str());
        }
        ImGui::EndGroup();

        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 96.0f);
        if (entry.relationship == "friends") {
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(paletteColor(kGreen), "Friends");
        } else if (entry.relationship == "request_sent") {
            ImGui::BeginDisabled(true);
            ImGui::Button("Pending", ImVec2(96.0f, 28.0f));
            ImGui::EndDisabled();
        } else if (entry.relationship == "request_received") {
            pushPrimaryActionButtonColors();
            if (ImGui::Button("Accept", ImVec2(96.0f, 28.0f))) {
                std::string error;
                (void)kronosApi_.respondToFriendRequest(entry.id, true, error);
                startFriendsFetch();
            }
            popPrimaryActionButtonColors();
        } else {
            pushPrimaryActionButtonColors();
            if (ImGui::Button("Add Friend", ImVec2(96.0f, 28.0f))) startFriendRequest(entry.id);
            popPrimaryActionButtonColors();
        }

        ImGui::EndChild();
        ImGui::PopID();
    }
    ImGui::EndChild();

    ImGui::Separator();
    if (ImGui::Button("Close", ImVec2(110.0f, 30.0f))) {
        ImGui::CloseCurrentPopup();
        friendSearchResults_.clear();
        friendSearchStatus_.clear();
        std::memset(friendSearchBuffer_, 0, sizeof(friendSearchBuffer_));
    }
    ImGui::EndPopup();
}

void RuntimeShell::drawFriendsCarousel() {
    using namespace core::kronos_palette;
    std::optional<core::KronosUser> user = kronosApi_.currentUser();

    size_t friendCount = friends_.size();
    std::string heading = friendCount > 0 ? "Friends (" + std::to_string(friendCount) + ")" : "Friends";
    ImGui::SeparatorText(heading.c_str());

    if (!user.has_value()) {
        ImGui::TextColored(paletteColor(kTextMuted), "Sign in to see who's online.");
        return;
    }

    constexpr float kCardSize = 84.0f;
    constexpr float kBadgeRadius = 30.0f;
    ImGui::BeginChild("##friends_carousel", ImVec2(0.0f, kCardSize + 34.0f), false,
                       ImGuiWindowFlags_HorizontalScrollbar);

    // Add Friends action card, always first.
    {
        ImGui::BeginGroup();
        ImVec2 origin = ImGui::GetCursorScreenPos();
        ImVec2 center(origin.x + kCardSize * 0.5f, origin.y + kBadgeRadius + 4.0f);
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddCircleFilled(center, kBadgeRadius, ImGui::GetColorU32(paletteColor(kSlate)), 32);
        drawList->AddCircle(center, kBadgeRadius, ImGui::GetColorU32(paletteColor(kSkyBlue)), 32, 2.0f);
        // A plus sign, drawn rather than relying on a glyph the font may
        // not carry.
        drawList->AddLine(ImVec2(center.x - 11.0f, center.y), ImVec2(center.x + 11.0f, center.y),
                           ImGui::GetColorU32(paletteColor(kSkyBlue)), 2.5f);
        drawList->AddLine(ImVec2(center.x, center.y - 11.0f), ImVec2(center.x, center.y + 11.0f),
                           ImGui::GetColorU32(paletteColor(kSkyBlue)), 2.5f);

        ImGui::InvisibleButton("##add_friends", ImVec2(kCardSize, kBadgeRadius * 2.0f + 8.0f));
        if (ImGui::IsItemClicked()) {
            bool isGuest = user->email.empty() && user->displayName.rfind("Guest", 0) == 0;
            if (isGuest) {
                // Guests are refused the social graph by the SERVER too;
                // this just explains why before the round trip.
                showGuestUpgradePrompt_ = true;
            } else {
                showAddFriendsModal_ = true;
                friendSearchResults_.clear();
                friendSearchStatus_.clear();
            }
        }
        ImGui::TextColored(paletteColor(kTextMuted), "Add");
        ImGui::EndGroup();
    }

    for (const core::KronosFriend& friendEntry : friends_) {
        ImGui::SameLine();
        ImGui::PushID(friendEntry.id.c_str());
        ImGui::BeginGroup();

        ImVec2 origin = ImGui::GetCursorScreenPos();
        ImVec2 center(origin.x + kCardSize * 0.5f, origin.y + kBadgeRadius + 4.0f);
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddCircleFilled(center, kBadgeRadius, ImGui::GetColorU32(paletteColor(kSlate)), 32);
        drawAvatarHeadGlyph(drawList, center, kBadgeRadius * 0.66f);

        // Status ring + dot: green = in the launcher, sky blue = in a
        // game, nothing at all when offline (an offline friend should not
        // wear a badge that implies presence).
        const bool inGame = friendEntry.status == "in_game";
        const bool online = inGame || friendEntry.status == "online_launcher";
        if (online) {
            ImVec4 statusColor = inGame ? paletteColor(kSkyBlue) : paletteColor(kGreen);
            drawList->AddCircle(center, kBadgeRadius, ImGui::GetColorU32(statusColor), 32, 2.5f);
            ImVec2 dot(center.x + kBadgeRadius * 0.70f, center.y + kBadgeRadius * 0.70f);
            drawList->AddCircleFilled(dot, 7.0f, ImGui::GetColorU32(statusColor), 16);
            drawList->AddCircle(dot, 7.0f, ImGui::GetColorU32(paletteColor(kCharcoal)), 16, 2.0f);
        }

        ImGui::InvisibleButton("##friend", ImVec2(kCardSize, kBadgeRadius * 2.0f + 8.0f));
        bool hovered = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked() && inGame && !friendEntry.joinTicket.empty()) {
            joinFriendGame(friendEntry);
        }
        if (hovered) {
            ImGui::BeginTooltip();
            ImGui::TextColored(paletteColor(kTextBright), "%s", friendEntry.username.empty()
                                                                    ? friendEntry.displayName.c_str()
                                                                    : friendEntry.username.c_str());
            if (inGame) {
                ImGui::TextColored(paletteColor(kSkyBlue), "In game");
                ImGui::TextColored(paletteColor(kTextMuted), "Click to join their server");
            } else if (online) {
                ImGui::TextColored(paletteColor(kGreen), "Online");
            } else {
                ImGui::TextColored(paletteColor(kTextMuted), "Offline");
            }
            ImGui::EndTooltip();
        }

        std::string label = friendEntry.username.empty() ? friendEntry.displayName : friendEntry.username;
        if (label.size() > 10) label = label.substr(0, 9) + "...";
        ImGui::TextColored(online ? paletteColor(kTextBright) : paletteColor(kTextMuted), "%s", label.c_str());

        ImGui::EndGroup();
        ImGui::PopID();
    }

    ImGui::EndChild();

    if (!friendsStatusMessage_.empty()) {
        ImGui::TextColored(paletteColor(kTextMuted), "%s", friendsStatusMessage_.c_str());
    }
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
    // Inset into the shell chrome's content canvas -- drawing at full
    // viewport size here is what put this panel underneath the sidebar
    // and brand panel.
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + kSidebarWidth, viewport->WorkPos.y + kTopBarHeight));
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x - kSidebarWidth - kBrandPanelWidth,
                                     viewport->WorkSize.y - kTopBarHeight));
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

// Kronos ("separate local games from the online feed"): the real
// disk-discovered games, now in their own tab rather than mixed into the
// main catalogue. Everything below is the same real scan/sort/row logic
// as before -- only where it is drawn changed.
void RuntimeShell::drawLocalGamesTab() {
    if (discoveredGames_.empty()) {
        ImGui::TextDisabled(
            "No real games found in games/ -- see docs/QUICKSTART.md for the real games/<Name>/game.gamemanifest layout.");
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
}

void RuntimeShell::drawGameCataloguePanel() {
    using namespace core::kronos_palette;
    beginContentCanvas("Catalogue");

    // No Back button and no nested Discover/Create tab bar: the left
    // sidebar is the only navigation, and duplicating it inside the
    // canvas was exactly the clutter this removes.
    if (catalogueTab_ == CatalogueTab::Create) {
        ImGui::TextColored(paletteColor(kTextBright), "Create");
        ImGui::TextColored(paletteColor(kTextMuted),
                            "Your local projects and Kronos Studio. Everything here works with no network.");
        ImGui::Dummy(ImVec2(0.0f, 12.0f));

        // Prominent, first: opening Studio is the primary action of this
        // tab, so it is not buried under a project list.
        pushPrimaryActionButtonColors();
        if (ImGui::Button("Launch Kronos Studio", ImVec2(240.0f, 42.0f))) launchStudio();
        popPrimaryActionButtonColors();
        if (!studioLaunchError_.empty()) {
            ImGui::TextColored(ImVec4(0.85f, 0.35f, 0.30f, 1.0f), "%s", studioLaunchError_.c_str());
        }

        ImGui::Dummy(ImVec2(0.0f, 16.0f));
        ImGui::SeparatorText("Local projects");
        ImGui::TextColored(paletteColor(kTextMuted),
                            "Discovered in this machine's games/ folder. Not published to Kronos -- nobody else can "
                            "see them. Play launches locally, with no join ticket and no server allocation.");
        ImGui::Dummy(ImVec2(0.0f, 8.0f));
        drawLocalGamesTab();
    } else {
        ImGui::TextColored(paletteColor(kTextBright), "Discover");
        ImGui::Dummy(ImVec2(0.0f, 8.0f));
        drawOnlineCatalogueSection();
    }

    endContentCanvas();
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
    // Not gated on state_: the sidebar routes here from any tab.
    ensureLocalProfileLoaded();
    ensureAvatarCatalogueLoaded();
    state_ = computeNextState(state_, ShellEvent::OpenAvatarShop);
}

void RuntimeShell::drawAvatarShopPanel() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    // Inset into the shell chrome's content canvas -- drawing at full
    // viewport size here is what put this panel underneath the sidebar
    // and brand panel.
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + kSidebarWidth, viewport->WorkPos.y + kTopBarHeight));
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x - kSidebarWidth - kBrandPanelWidth,
                                     viewport->WorkSize.y - kTopBarHeight));
    ImGui::Begin("Avatar Shop", nullptr,
                  ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);

    // Kronos: the interactive 3D avatar viewport, re-embedded here.
    //
    // It used to be a floating window hovering over the middle of the
    // Home screen, which is why it was removed from Home -- but removing
    // it entirely lost a real feature. It belongs on the Avatar tab, in a
    // padded panel beside the item controls, where previewing the
    // character while changing it is the whole point. Only drawn on this
    // tab, so it costs nothing to render anywhere else.
    if (!showAvatarShopOverlay_) {
        // Side-by-side: orbit viewport left, gear/catalogue right, both in
        // slate cards. Previewing the character while changing it is the
        // whole point of this tab, so they belong beside each other rather
        // than stacked with the items pushed off-screen.
        ensureHomeAvatarPreviewLoaded();
        constexpr float kViewportWidth = 360.0f;
        float columnHeight = ImGui::GetContentRegionAvail().y - 6.0f;
        if (columnHeight < 240.0f) columnHeight = 240.0f;

        ImGui::BeginChild("##avatar_viewport_card", ImVec2(kViewportWidth, columnHeight), true);
        ImGui::TextColored(paletteColor(core::kronos_palette::kTextBright), "Your Avatar");
        ImGui::TextColored(paletteColor(core::kronos_palette::kTextMuted), "Drag to orbit, scroll to zoom");
        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        // Fills the rest of the card, so it scales with the window rather
        // than being pinned to one size.
        ImGui::BeginChild("##avatar_viewport_inner", ImVec2(0.0f, 0.0f), false);
        if (homeAvatarPreview_) homeAvatarPreview_->draw();
        ImGui::EndChild();
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginChild("##avatar_items_card", ImVec2(0.0f, columnHeight), true);
    }

    // Kronos ("Marketplace" -- "engine_runtime-side catalogue UI" --
    // live re-equip while InGame): real -- when opened as the InGame HUD
    // overlay (see showAvatarShopOverlay_'s own comment), "Back" closes
    // the overlay and resumes movement input instead of real-transitioning
    // ShellState (there is no "Home" to transition to -- a real game is
    // still live underneath).
    // "Back" survives ONLY as the in-game overlay's close control. As a
    // sidebar-routed tab there is nothing to go back to, so the
    // standalone Back button is gone.
    if (showAvatarShopOverlay_) {
        if (ImGui::Button("Close")) {
            showAvatarShopOverlay_ = false;
            avatarShopDetailOpen_ = false;
            app_.setMovementInputSuspended(false);
            ImGui::End();
            return;
        }
        ImGui::SameLine();
    }
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

    if (!showAvatarShopOverlay_) ImGui::EndChild(); // right-hand items card

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
            pushAvatarConfigToBackend();
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
                pushAvatarConfigToBackend();
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

// Kronos ("Settings Panel v2" -- "Window/Fullscreen scaling"): real, same
// "small honest list, not free-form input" convention every other real
// settings choice in this file already uses -- only meaningful while
// windowed (see LocalProfile::windowResolutionIndex's own comment).
struct WindowResolutionPreset {
    const char* label;
    uint32_t width;
    uint32_t height;
};
constexpr WindowResolutionPreset kWindowResolutionPresets[] = {
    {"1280 x 720", 1280, 720},
    {"1600 x 900", 1600, 900},
    {"1920 x 1080", 1920, 1080},
    {"2560 x 1440", 2560, 1440},
};
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
    // Kronos ("Washed-Out Lighting" fix): real root cause of the
    // live-reported "blinding white ambient fog" complaint -- neither
    // applyQualityPreset() above (Medium/High both call
    // setVolumetricFogEnabled(true)) nor the explicit profile override
    // just above it ever called setVolumetricFogParams(), so a fresh
    // LocalProfile (qualityPresetIndex defaults to 1/Medium,
    // volumetricFogEnabled defaults to true -- see that field's own
    // comment) left the pass running with core::Renderer's raw,
    // never-tuned class defaults (scatteringIntensity 1.0, 20 steps,
    // 120 units -- see Renderer.hpp's own volumetricFogScatteringIntensity_
    // field). shaders/volumetric_fog.frag's own stepRadiance term sums
    // that scatteringIntensity against the *directional sun's* own
    // intensity (up to 4.0, see TimeOfDay.cpp's kMaxIntensity) over every
    // screen pixel -- background sky rays included, not just real
    // geometry -- which is exactly why the previous pass at TimeOfDay.cpp's
    // own ambient/fogDensity values (scene.frag's separate, much weaker
    // per-fragment fog) had no visible effect: this raymarch pass was the
    // actual dominant haze, and nothing in this file ever tuned it for a
    // real outdoor gameplay camera. Real, already-proven-good values --
    // the exact same tuning main.cpp's own --tntwars mode already ships
    // with (see its setVolumetricFogParams() call) -- applied here
    // unconditionally so every combination of preset/override that can
    // leave volumetric fog enabled gets real, sane params instead of
    // sometimes falling through to the untuned class defaults.
    app_.renderer().setVolumetricFogParams(0.15f, 24, 180.0f);
    // Kronos ("Cloud Encapsulation / Skybox Clipping" fix -- live-reported
    // issue: the fog reads as a dense dome that completely encloses the
    // map, hiding the sun/skybox from a normal standing camera and only
    // clearing up when looking up from under real geometry): real root
    // cause -- core::Renderer::setVolumetricFogHeightGradient() has
    // existed since the "Real-Time Rendering Evolved" trailer work (see
    // its own header comment) but was never actually called by anything,
    // anywhere in this codebase (confirmed by a full-repo grep) -- so
    // every real caller silently kept its documented 1/1 no-op default:
    // *zero* density falloff with altitude. shaders/volumetric_fog.frag's
    // raymarch treats an open-sky ray exactly like a ray toward nearby
    // ground -- full density for the entire real march distance -- which
    // is exactly why the sun/sky only "poke through" when a real, much
    // shorter occluder (a roof, a platform from underneath) cuts the
    // march short instead: that's not a real depth-buffer or skybox bug,
    // it's this pass integrating uniform fog through open sky as if it
    // were solid haze all the way up. Real fix: a real height gradient --
    // full ground-level density unchanged (groundDensityMultiplier 1.0,
    // groundHeightY 0.0, matching real outdoor haze at foot level) that
    // falls off to a real, thin upper-atmosphere density
    // (aloftDensityMultiplier 0.08) within falloffHeight (40 real world
    // units -- comfortably above normal jumping/building height in these
    // maps, well short of the pass's own 180-unit maxDistance above), so
    // an upward-facing ray spends the vast majority of its real march
    // length in that thin aloft layer instead of accumulating a full
    // dome's worth of extinction/in-scattering.
    app_.renderer().setVolumetricFogHeightGradient(1.0f, 0.08f, 0.0f, 40.0f);
    app_.renderer().setVsyncEnabled(localProfile_.vsyncEnabled);
    // Kronos ("Settings Panel v2" -- "Window/Fullscreen scaling"): real
    // -- applies a saved fullscreen/resolution choice on startup, same
    // as every other real graphics setting here. Fullscreen wins over a
    // stored windowed resolution (matches setSize()'s own documented
    // "no-op while fullscreen" behavior -- applying both in the wrong
    // order would silently drop the resolution choice on the floor).
    app_.window().setFullscreen(localProfile_.fullscreenEnabled);
    if (!localProfile_.fullscreenEnabled) {
        int resIndex = std::clamp(localProfile_.windowResolutionIndex, 0,
                                   static_cast<int>(IM_ARRAYSIZE(kWindowResolutionPresets)) - 1);
        app_.window().setSize(kWindowResolutionPresets[resIndex].width, kWindowResolutionPresets[resIndex].height);
    }
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
    // Not gated on state_: the sidebar routes here from any tab.
    ensureLocalProfileLoaded();
    state_ = computeNextState(state_, ShellEvent::OpenSettings);
}

void RuntimeShell::drawSettingsPanel() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    // Kronos ("Graphics Settings Z-Order" fix -- live-reported issue:
    // opening Settings from the in-game pause menu drew it *behind* that
    // menu): this same function serves two real, different presentations
    // -- a full-canvas "page" reached from Home's own sidebar
    // (showSettingsOverlay_ == false, state_ == ShellState::Settings),
    // where NoBringToFrontOnFocus/NoMove/the sidebar+brand-panel inset
    // are all real and correct (it's meant to sit in the shell chrome's
    // content canvas, never fighting z-order with sibling "pages"), and
    // a real floating overlay opened on top of live gameplay
    // (showSettingsOverlay_ == true, from the pause menu's own "Graphics
    // Settings" button) that was still using those exact same
    // page-oriented flags/positioning -- NoBringToFrontOnFocus is
    // precisely why ImGui could never raise it above the pause menu
    // window drawn immediately before it. The overlay case now draws as
    // a real, independently-focusable, centered floating window instead,
    // matching drawPlayerListOverlay()'s own pause-menu presentation.
    if (showSettingsOverlay_) {
        ImVec2 center(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f, viewport->WorkPos.y + viewport->WorkSize.y * 0.5f);
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(std::min(700.0f, viewport->WorkSize.x - 64.0f),
                                         std::min(600.0f, viewport->WorkSize.y - 64.0f)),
                                  ImGuiCond_Appearing);
        ImGui::Begin("Settings", nullptr, ImGuiWindowFlags_NoCollapse);
    } else {
        // Inset into the shell chrome's content canvas -- drawing at full
        // viewport size here is what put this panel underneath the sidebar
        // and brand panel.
        ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + kSidebarWidth, viewport->WorkPos.y + kTopBarHeight));
        ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x - kSidebarWidth - kBrandPanelWidth,
                                         viewport->WorkSize.y - kTopBarHeight));
        ImGui::Begin("Settings", nullptr,
                      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);
    }

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
    // Kronos ("Settings Panel v2" -- "Window/Fullscreen scaling"): real
    // -- core::Window::setFullscreen()/setSize() actually switch the
    // live window; the existing resize-triggered
    // Renderer::recreateSwapchain() path (already exercised by vsync
    // toggling) handles the Vulkan side automatically.
    if (ImGui::Checkbox("Fullscreen", &localProfile_.fullscreenEnabled)) {
        app_.window().setFullscreen(localProfile_.fullscreenEnabled);
        changed = true;
    }
    ImGui::BeginDisabled(localProfile_.fullscreenEnabled);
    static constexpr const char* kResolutionNames[] = {
        kWindowResolutionPresets[0].label, kWindowResolutionPresets[1].label, kWindowResolutionPresets[2].label,
        kWindowResolutionPresets[3].label,
    };
    if (ImGui::Combo("Resolution", &localProfile_.windowResolutionIndex, kResolutionNames,
                      IM_ARRAYSIZE(kResolutionNames))) {
        const WindowResolutionPreset& preset = kWindowResolutionPresets[localProfile_.windowResolutionIndex];
        app_.window().setSize(preset.width, preset.height);
        changed = true;
    }
    ImGui::EndDisabled();
    if (localProfile_.fullscreenEnabled) {
        ImGui::SameLine();
        ImGui::TextDisabled("(windowed only)");
    }

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

// ---------------------------------------------------------------------------
// Kronos backend integration
// ---------------------------------------------------------------------------

// Kronos Client spec, Authentication Flow: the launcher's ONLY sign-in
// entry point. It opens the system browser and waits on a real loopback
// callback, so no username or password ever passes through this process
// -- which is precisely why the spec forbids credential fields here.
//
// The web page is expected to redirect back to
// http://127.0.0.1:<port>/auth/callback?code=<refresh_token>&state=<state>.
// Reusing the existing LoopbackHttpServer (built for the Google OAuth
// flow) means this needs no new client plumbing, and carrying a refresh
// token means it needs no new BACKEND endpoint either -- the exchange
// goes through the same /v1/auth/refresh an ordinary resume uses.
void RuntimeShell::startBrowserSignIn() {
    if (backendAuthInProgress_.load()) return;
    if (backendAuthThread_.joinable()) backendAuthThread_.join();

    backendAuthInProgress_.store(true);
    backendAuthStatusMessage_ = "Continue in your browser to finish signing in...";

    backendAuthThread_ = std::thread([this]() {
        core::KronosAuthResult result;

        constexpr uint16_t kCallbackPort = 8765;
        core::LoopbackHttpServer loopback;
        if (!loopback.start(kCallbackPort)) {
            result.error = "Could not open the local sign-in listener (is port 8765 already in use?).";
            std::lock_guard<std::mutex> lock(backendAuthMutex_);
            backendAuthPendingResult_ = std::move(result);
            backendAuthInProgress_.store(false);
            return;
        }

        // Real CSRF state, checked against what comes back -- so this
        // process only ever accepts a redirect it itself initiated.
        std::string state = core::generateCodeVerifier().substr(0, 32);
        std::string redirectUri = "http://127.0.0.1:" + std::to_string(kCallbackPort) + "/auth/callback";
        std::string authUrl = resolveKronosAuthUrl() + "?redirect_uri=" + urlEncodeComponent(redirectUri) +
                               "&state=" + urlEncodeComponent(state);

        if (!core::openUrlInDefaultBrowser(authUrl)) {
            loopback.stop();
            result.error = "Could not open your web browser to sign in.";
            std::lock_guard<std::mutex> lock(backendAuthMutex_);
            backendAuthPendingResult_ = std::move(result);
            backendAuthInProgress_.store(false);
            return;
        }

        // Bounded: a user who closes the tab must not leave this thread
        // (and the port) pinned forever.
        core::LoopbackCallbackResult callback = loopback.waitForCallback(180.0f);
        loopback.stop();

        if (!callback.success) {
            result.error = callback.error.empty() ? "Sign-in was not completed." : callback.error;
        } else if (callback.state != state) {
            // Fail closed: a mismatched state means this redirect was not
            // the one we started.
            result.error = "Sign-in could not be verified. Please try again.";
        } else {
            result = kronosApi_.completeBrowserSignIn(callback.code);
        }

        std::lock_guard<std::mutex> lock(backendAuthMutex_);
        backendAuthPendingResult_ = std::move(result);
        backendAuthInProgress_.store(false);
    });
}

std::string RuntimeShell::backendStatusLine() const {
    switch (backendReachability_) {
        case BackendReachability::Reachable: return "Connected to Kronos services";
        case BackendReachability::Unreachable: return "Offline -- Local / Dev games still work";
        case BackendReachability::Unknown: break;
    }
    return "Connecting to Server Status...";
}

// ---------------------------------------------------------------------------
// Kronos Client v0.2.0-alpha shell chrome
//
// The three fixed regions from the visual spec: a slate sidebar on the
// left, a top bar above the content, and a brand panel on the right.
// Drawn as separate borderless ImGui windows rather than one window with
// manual cursor math -- each region then clips and scrolls independently,
// which is what makes the content area able to scroll a long game grid
// without dragging the sidebar with it.
// ---------------------------------------------------------------------------

void RuntimeShell::drawSidebar() {
    using namespace core::kronos_palette;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();

    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(ImVec2(kSidebarWidth, viewport->WorkSize.y));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, paletteColor(kSlate));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 16.0f));
    ImGui::Begin("##kronos_sidebar", nullptr,
                  ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);

    if (ImFont* bold = core::kronosBoldFont()) ImGui::PushFont(bold);
    ImGui::SetWindowFontScale(1.25f);
    ImGui::TextColored(paletteColor(kTextBright), "KRONOS");
    ImGui::SetWindowFontScale(1.0f);
    if (core::kronosBoldFont()) ImGui::PopFont();
    ImGui::Dummy(ImVec2(0.0f, 12.0f));

    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##sidebar_search", "Search...", searchBuffer_, sizeof(searchBuffer_));
    ImGui::Dummy(ImVec2(0.0f, 14.0f));

    // Each entry maps to a real ShellState the shell already knows how to
    // draw -- the sidebar is navigation, not a second state machine.
    struct NavEntry {
        const char* label;
        const char* subtitle;
        ShellState target;
    };
    static const NavEntry kNav[] = {
        {"Home", nullptr, ShellState::Home},
        {"Discover", nullptr, ShellState::GameCatalogue},
        {"Avatar", nullptr, ShellState::AvatarShop},
        {"Directory", "Accounts & presence", ShellState::Friends},
        {"Create", "Studio & Local Dev Games", ShellState::Home}, // Create is a mode of the catalogue, see below
        {"Settings", nullptr, ShellState::Settings},
    };

    for (const NavEntry& entry : kNav) {
        bool isCreate = std::strcmp(entry.label, "Create") == 0;
        bool selected = isCreate ? (state_ == ShellState::GameCatalogue && catalogueTab_ == CatalogueTab::Create)
                                  : (state_ == entry.target &&
                                     !(entry.target == ShellState::GameCatalogue && catalogueTab_ == CatalogueTab::Create));

        // Sky blue marks where you are; everything else stays flat slate.
        ImGui::PushStyleColor(ImGuiCol_Button, selected ? paletteColor(kSkyBlue) : ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                               selected ? paletteColor(kSkyBlue) : ImVec4(1.0f, 1.0f, 1.0f, 0.06f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, paletteColor(kSkyBlue));
        ImGui::PushStyleColor(ImGuiCol_Text, selected ? ImVec4(0.06f, 0.09f, 0.11f, 1.0f) : paletteColor(kTextMuted));

        float height = entry.subtitle != nullptr ? 42.0f : 34.0f;
        if (ImGui::Button(entry.label, ImVec2(-1.0f, height))) {
            if (isCreate) {
                catalogueTab_ = CatalogueTab::Create;
                state_ = ShellState::GameCatalogue;
                openGameCatalogue();
            } else if (entry.target == ShellState::GameCatalogue) {
                catalogueTab_ = CatalogueTab::Discover;
                state_ = ShellState::GameCatalogue;
                openGameCatalogue();
            } else {
                state_ = entry.target;
                // Route through the same real open* helpers the old menu
                // used, so each destination still loads what it needs.
                if (entry.target == ShellState::AvatarShop) openAvatarShop();
                else if (entry.target == ShellState::Settings) openSettings();
                else if (entry.target == ShellState::Friends) {
                    directoryNextCursor_.clear();
                    startDirectoryFetch(/*loadNextPage=*/false);
                }
            }
        }
        ImGui::PopStyleColor(4);

        if (entry.subtitle != nullptr) {
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 12.0f);
            ImGui::TextColored(paletteColor(kTextMuted), "   %s", entry.subtitle);
        }
        ImGui::Dummy(ImVec2(0.0f, 4.0f));
    }

    // Version, pinned to the bottom.
    ImGui::SetCursorPosY(viewport->WorkSize.y - 32.0f);
    ImGui::TextColored(paletteColor(kTextMuted), "Kronos Client v%s", core::kKronosVersion);

    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

void RuntimeShell::drawTopBar() {
    using namespace core::kronos_palette;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    float contentWidth = viewport->WorkSize.x - kSidebarWidth - kBrandPanelWidth;

    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + kSidebarWidth, viewport->WorkPos.y));
    ImGui::SetNextWindowSize(ImVec2(contentWidth, kTopBarHeight));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, paletteColor(kCharcoal));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 14.0f));
    ImGui::Begin("##kronos_topbar", nullptr,
                  ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);

    ImGui::SetNextItemWidth(contentWidth * 0.42f);
    ImGui::InputTextWithHint("##topbar_search", "Search...", searchBuffer_, sizeof(searchBuffer_));

    // Right cluster: profile card, notification bell, sign-in button.
    std::optional<core::KronosUser> user = kronosApi_.currentUser();
    const char* profileLabel = user.has_value()
                                    ? (user->displayName.empty() ? user->email.c_str() : user->displayName.c_str())
                                    : "Player";

    float signInWidth = user.has_value() ? 92.0f : 132.0f;
    float rightClusterWidth = 150.0f + 34.0f + signInWidth + 24.0f;
    ImGui::SameLine(contentWidth - rightClusterWidth);

    // Profile card -- the smooth fixed-shape avatar head, drawn
    // procedurally so it matches the character silhouette without needing
    // a separate portrait asset per user.
    ImVec2 headOrigin = ImGui::GetCursorScreenPos();
    drawAvatarHeadGlyph(ImGui::GetWindowDrawList(), ImVec2(headOrigin.x + 15.0f, headOrigin.y + 16.0f), 13.0f);
    ImGui::Dummy(ImVec2(34.0f, 32.0f));
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(paletteColor(kTextBright), "%s", profileLabel);

    ImGui::SameLine();
    if (ImGui::Button(notification::unreadCount(localProfile_) > 0 ? "(*)" : "( )", ImVec2(30.0f, 30.0f))) {
        state_ = ShellState::Notifications;
    }

    ImGui::SameLine();
    if (user.has_value()) {
        if (ImGui::Button("Sign Out", ImVec2(signInWidth, 30.0f))) backendSignOut();
    } else {
        // Spec: the launcher never shows a username or password field.
        // This button's ONLY job is to hand off to the system browser.
        pushPrimaryActionButtonColors();
        if (ImGui::Button(backendAuthInProgress_.load() ? "Waiting..." : "Sign In / Sign Up",
                           ImVec2(signInWidth, 30.0f))) {
            if (!backendAuthInProgress_.load()) startBrowserSignIn();
        }
        popPrimaryActionButtonColors();
    }

    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

void RuntimeShell::drawBrandPanel() {
    using namespace core::kronos_palette;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();

    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - kBrandPanelWidth, viewport->WorkPos.y));
    ImGui::SetNextWindowSize(ImVec2(kBrandPanelWidth, viewport->WorkSize.y));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, paletteColor(kCharcoal));
    ImGui::Begin("##kronos_brand", nullptr,
                  ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);

    ImVec2 center(ImGui::GetWindowPos().x + kBrandPanelWidth * 0.5f,
                   ImGui::GetWindowPos().y + viewport->WorkSize.y * 0.38f);
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    // Concentric sky-blue rings, then the existing procedural hourglass
    // in the middle -- reusing the real one the loading screen already
    // draws rather than introducing a separate art asset.
    ImU32 ringColor = ImGui::GetColorU32(ImVec4(kSkyBlue[0], kSkyBlue[1], kSkyBlue[2], 0.55f));
    drawList->AddCircle(center, 118.0f, ringColor, 96, 2.5f);
    drawList->AddCircle(center, 104.0f, ImGui::GetColorU32(ImVec4(kSkyBlue[0], kSkyBlue[1], kSkyBlue[2], 0.25f)), 96,
                         1.5f);
    drawAnimatedHourglass(drawList, center, 44.0f, 62.0f, static_cast<float>(ImGui::GetTime()));

    ImGui::SetCursorPosY(viewport->WorkSize.y * 0.38f + 150.0f);
    auto centeredText = [&](const ImVec4& color, const char* text) {
        float width = ImGui::CalcTextSize(text).x;
        ImGui::SetCursorPosX((kBrandPanelWidth - width) * 0.5f);
        ImGui::TextColored(color, "%s", text);
    };
    centeredText(paletteColor(kTextBright), "KRONOS");
    std::string versionLine = std::string("Kronos Client v") + core::kKronosVersion;
    centeredText(paletteColor(kTextMuted), versionLine.c_str());
    // Real status, not decorative text: this reflects whether the last
    // real backend call actually reached the service.
    centeredText(paletteColor(kTextMuted), backendStatusLine().c_str());

    ImGui::End();
    ImGui::PopStyleColor();
}


void RuntimeShell::startBackendSessionRestore() {
    if (backendAuthInProgress_.load()) return;
    if (backendAuthThread_.joinable()) backendAuthThread_.join();

    backendAuthInProgress_.store(true);
    backendAuthThread_ = std::thread([this]() {
        core::KronosAuthResult result = kronosApi_.restoreSession();
        std::lock_guard<std::mutex> lock(backendAuthMutex_);
        // A failed restore at launch is a real non-event: it just means
        // nobody is signed in yet. Only a real success is surfaced, so
        // "No saved Kronos session." never appears as an error to
        // somebody who simply has not signed in.
        if (result.success) backendAuthPendingResult_ = std::move(result);
        backendAuthInProgress_.store(false);
    });
}

void RuntimeShell::startHandoffExchange(std::string code) {
    if (backendAuthInProgress_.load()) return;
    if (backendAuthThread_.joinable()) backendAuthThread_.join();

    backendAuthInProgress_.store(true);
    backendAuthStatusMessage_ = "Signing you in from the browser...";

    backendAuthThread_ = std::thread([this, code = std::move(code)]() {
        core::KronosAuthResult result = kronosApi_.exchangeHandoffCode(code);
        std::lock_guard<std::mutex> lock(backendAuthMutex_);
        // Unlike startBackendSessionRestore()'s silent-on-failure
        // handling, BOTH outcomes are surfaced here -- a code that fails
        // to exchange (expired, already used, network hiccup) means the
        // launch this process exists to fulfil cannot complete as
        // requested, and that is worth telling the person who clicked
        // the button, not hiding.
        backendAuthPendingResult_ = std::move(result);
        backendAuthInProgress_.store(false);
    });
}

void RuntimeShell::startCatalogueFetch(bool loadNextPage) {
    if (catalogueFetchInProgress_.load()) return;
    // Nothing more to load: without this the grid would re-request the
    // last page every time the user scrolled to the bottom.
    if (loadNextPage && catalogueNextCursor_.empty()) return;
    if (catalogueFetchThread_.joinable()) catalogueFetchThread_.join();

    catalogueFetchInProgress_.store(true);
    catalogueAppendingPage_ = loadNextPage;
    catalogueStatusMessage_ = loadNextPage ? "Loading more..." : "Loading games from Kronos...";

    const std::string cursor = loadNextPage ? catalogueNextCursor_ : std::string();
    catalogueFetchThread_ = std::thread([this, cursor]() {
        // 200 is the batch size the backend caps at -- one round trip per
        // screenful of grid rather than per handful of cards.
        core::CatalogueResult result = kronosApi_.fetchGames(kCatalogueBatchSize, cursor);
        std::lock_guard<std::mutex> lock(catalogueFetchMutex_);
        cataloguePendingResult_ = std::move(result);
        catalogueFetchInProgress_.store(false);
    });
}

void RuntimeShell::startServerAllocation(const std::string& gameSlug, const std::string& title) {
    if (allocationInProgress_.load()) return;
    if (allocationThread_.joinable()) allocationThread_.join();
    if (friendsThread_.joinable()) friendsThread_.join();
    if (friendSearchThread_.joinable()) friendSearchThread_.join();
    if (presenceThread_.joinable()) presenceThread_.join();
    if (directoryThread_.joinable()) directoryThread_.join();

    allocationInProgress_.store(true);
    allocationGameTitle_ = title;
    allocationGameSlug_ = gameSlug;
    allocationThread_ = std::thread([this, gameSlug]() {
        core::ServerAllocation result = kronosApi_.allocateServer(gameSlug);
        std::lock_guard<std::mutex> lock(allocationMutex_);
        allocationPendingResult_ = std::move(result);
        allocationInProgress_.store(false);
    });
}

void RuntimeShell::backendSignOut() {
    // logOut() makes a real (best-effort) network call, so it goes on a
    // background thread too rather than stalling this frame.
    if (backendAuthThread_.joinable()) backendAuthThread_.join();
    backendAuthThread_ = std::thread([this]() { kronosApi_.logOut(); });
    onlineGames_.clear();
    onlinePlayerCountsAvailable_ = false;
    catalogueStatusMessage_.clear();
    backendAuthStatusMessage_.clear();
    notify(core::NotificationKind::SystemMessage, "Signed out", "You have been signed out of Kronos.");
}

void RuntimeShell::pollBackendResults() {
    // --- sign-in / sign-up / restore ---
    {
        std::optional<core::KronosAuthResult> authResult;
        {
            std::lock_guard<std::mutex> lock(backendAuthMutex_);
            if (backendAuthPendingResult_.has_value()) {
                authResult = std::move(backendAuthPendingResult_);
                backendAuthPendingResult_.reset();
            }
        }
        if (authResult.has_value()) {
            if (authResult->success) {
                backendAuthStatusMessage_.clear();
                // The password must not linger in process memory once it
                // has served its purpose.

                std::string who =
                    authResult->user.displayName.empty() ? authResult->user.email : authResult->user.displayName;
                notify(core::NotificationKind::SystemMessage, "Signed in", "Signed in to Kronos as " + who + ".");
                // Now that a real session exists, pull the real feed.
                startCatalogueFetch();
                // Kronos Avatar & Starter Marketplace Foundation: the
                // backend is authoritative the moment a real session
                // exists -- pulled here so signing in on a second
                // machine really shows the same look, not last device's
                // stale local file.
                pullAvatarConfigFromBackend();
            } else {
                backendAuthStatusMessage_ = authResult->error;
                // A pending kronos:// deep-link launch was waiting on
                // exactly this attempt succeeding (see
                // setPendingDeepLinkLaunch()'s own comment). It cannot
                // resolve now -- abandoning it here is what stops it
                // sitting forever hoping some later, unrelated sign-in
                // will complete it instead.
                pendingDeepLinkGameSlug_.clear();
            }
        }
    }

    // --- avatar config pull (Kronos Avatar & Starter Marketplace Foundation) ---
    {
        std::optional<core::AvatarConfig> avatarConfig;
        {
            std::lock_guard<std::mutex> lock(avatarConfigPullMutex_);
            if (avatarConfigPullPendingResult_.has_value()) {
                avatarConfig = std::move(avatarConfigPullPendingResult_);
                avatarConfigPullPendingResult_.reset();
            }
        }
        if (avatarConfig.has_value() && avatarConfig->success) {
            // Real catalogue must be loaded before any equip() call below
            // can possibly succeed -- equip() validates the item id
            // against avatarCatalogueIndex_, not just trusts it.
            ensureAvatarCatalogueLoaded();
            localProfile_.skinToneIndex = avatarConfig->skinToneIndex;
            localProfile_.headShapeIndex = avatarConfig->headShapeIndex;
            localProfile_.bodyHeight = avatarConfig->bodyHeight;
            localProfile_.bodyWidth = avatarConfig->bodyWidth;
            localProfile_.bodyLimbScale = avatarConfig->bodyLimbScale;
            localProfile_.bodyTorsoLength = avatarConfig->bodyTorsoLength;
            localProfile_.bodyShoulderWidth = avatarConfig->bodyShoulderWidth;
            localProfile_.clothingFitIndex = avatarConfig->clothingFitIndex;
            (void)localProfile_.saveToFile(kLocalProfilePath);

            // Real, honest degradation: an equipped item id the backend
            // remembers but THIS device's own local catalogue.json does
            // not know about (no backend-authoritative catalogue exists
            // yet -- see avatar/routes.js's own comment) simply cannot
            // be equipped here; equip() itself already reports that via
            // its return value, silently skipped rather than treated as
            // an error.
            avatarLoadout_.clear();
            for (const auto& [categoryName, itemId] : avatarConfig->equippedItems) {
                (void)avatarLoadout_.equip(itemId, avatarCatalogueIndex_);
            }
            (void)avatarLoadout_.saveToFile(kAvatarLoadoutPath);

            if (state_ == ShellState::InGame) {
                app_.refreshLocalPlayerAvatarAppearance(core::resolveSkinToneColor(localProfile_.skinToneIndex),
                                                          avatarLoadout_, avatarCatalogueIndex_);
            }
            if (homeAvatarPreview_) homeAvatarPreview_->refresh();
        }
    }

    // --- catalogue feed ---
    {
        std::optional<core::CatalogueResult> catalogueResult;
        {
            std::lock_guard<std::mutex> lock(catalogueFetchMutex_);
            if (cataloguePendingResult_.has_value()) {
                catalogueResult = std::move(cataloguePendingResult_);
                cataloguePendingResult_.reset();
            }
        }
        if (catalogueResult.has_value()) {
            // Real observed reachability, set only by a call that actually
            // completed -- never assumed.
            backendReachability_ =
                catalogueResult->success ? BackendReachability::Reachable : BackendReachability::Unreachable;
            if (catalogueResult->success) {
                if (catalogueAppendingPage_) {
                    // Locally cached: previously loaded batches stay in
                    // memory so scrolling back up never re-fetches.
                    onlineGames_.insert(onlineGames_.end(),
                                         std::make_move_iterator(catalogueResult->games.begin()),
                                         std::make_move_iterator(catalogueResult->games.end()));
                } else {
                    onlineGames_ = std::move(catalogueResult->games);
                }
                catalogueNextCursor_ = catalogueResult->nextCursor;
                catalogueAppendingPage_ = false;
                onlinePlayerCountsAvailable_ = catalogueResult->playerCountsAvailable;
                catalogueStatusMessage_ =
                    onlineGames_.empty() ? "No games have been published to Kronos yet." : std::string();
                core::logInfo("Kronos", "online catalogue: %zu game(s), player counts %s", onlineGames_.size(),
                              onlinePlayerCountsAvailable_ ? "available" : "unavailable");
            } else {
                // Deliberately does NOT clear onlineGames_: a transient
                // network blip should leave the last real feed on screen
                // rather than blanking it.
                catalogueAppendingPage_ = false;
                catalogueStatusMessage_ = catalogueResult->error;
                core::logWarn("Kronos", "online catalogue fetch failed: %s", catalogueResult->error.c_str());
            }
        }
    }

    // --- kronos:// deep-link resolution ---
    //
    // Checked independently of whether THIS poll cycle had a fresh
    // catalogue result: the pending slug can be set (via a launch URI)
    // either before or after the catalogue already finished loading, and
    // this has to resolve correctly either way. A linear scan over
    // onlineGames_ only runs at all while a slug is actually pending, so
    // this costs nothing on the overwhelming majority of frames where
    // pendingDeepLinkGameSlug_ is empty.
    //
    // Gated on backendAuthInProgress_ -- a real, previously-latent race,
    // not a hypothetical one: GET /v1/catalog/games needs no auth at
    // all, so the public catalogue can (and often does) finish loading
    // before a slower hand-off exchange (an extra real network round
    // trip beyond what a plain deep link needed) has landed. Resolving
    // against onlineGames_ the instant it is populated, without waiting
    // here, would fire startServerAllocation() with no access token yet
    // -- exactly the "Missing bearer token." this hand-off mechanism
    // exists to fix, just moved one step later instead of eliminated.
    if (!pendingDeepLinkGameSlug_.empty() && !backendAuthInProgress_.load()) {
        for (const core::CatalogueGame& game : onlineGames_) {
            if (game.slug != pendingDeepLinkGameSlug_) continue;
            // Cleared BEFORE starting the allocation, not after: this is
            // the real, existing "Play" button path (see
            // startServerAllocation()'s own header comment and its
            // ImGui::Button call site) -- calling it twice for the same
            // deep link would be a real double-allocation bug, not a
            // hypothetical one, if this ran again on the next poll before
            // the allocation thread's result had landed.
            std::string slug = pendingDeepLinkGameSlug_;
            std::string title = game.title;
            pendingDeepLinkGameSlug_.clear();
            core::logInfo("Kronos", "resolving kronos:// launch -- joining \"%s\" (%s)", title.c_str(), slug.c_str());
            startServerAllocation(slug, title);
            break;
        }
    }

    // --- friends list ---
    {
        std::optional<core::FriendsResult> friendsResult;
        {
            std::lock_guard<std::mutex> lock(friendsMutex_);
            if (friendsPendingResult_.has_value()) {
                friendsResult = std::move(friendsPendingResult_);
                friendsPendingResult_.reset();
            }
        }
        if (friendsResult.has_value()) {
            if (friendsResult->success) {
                friends_ = std::move(friendsResult->friends);
                friendsStatusMessage_ = friendsResult->presenceAvailable
                                             ? std::string()
                                             : std::string("Presence is unavailable right now.");
            } else {
                friendsStatusMessage_ = friendsResult->error;
            }
        }
    }

    // --- user search ---
    {
        std::optional<core::UserSearchResponse> searchResult;
        {
            std::lock_guard<std::mutex> lock(friendSearchMutex_);
            if (friendSearchPendingResult_.has_value()) {
                searchResult = std::move(friendSearchPendingResult_);
                friendSearchPendingResult_.reset();
            }
        }
        if (searchResult.has_value()) {
            if (searchResult->success) {
                friendSearchResults_ = std::move(searchResult->results);
                friendSearchStatus_.clear();
            } else {
                friendSearchResults_.clear();
                friendSearchStatus_ = searchResult->error;
            }
        }
    }

    // --- account directory ---
    {
        std::optional<core::DirectoryResult> page;
        std::optional<core::PresenceSummary> summary;
        {
            std::lock_guard<std::mutex> lock(directoryMutex_);
            if (directoryPendingResult_.has_value()) {
                page = std::move(directoryPendingResult_);
                directoryPendingResult_.reset();
            }
            if (directoryPendingSummary_.has_value()) {
                summary = std::move(directoryPendingSummary_);
                directoryPendingSummary_.reset();
            }
        }
        if (summary.has_value()) directorySummary_ = *summary;
        if (page.has_value()) {
            if (page->success) {
                if (directoryAppendingPage_) {
                    directoryUsers_.insert(directoryUsers_.end(), std::make_move_iterator(page->users.begin()),
                                            std::make_move_iterator(page->users.end()));
                } else {
                    directoryUsers_ = std::move(page->users);
                }
                directoryNextCursor_ = page->nextCursor;
                directoryStatusMessage_.clear();
            } else {
                directoryStatusMessage_ = page->error;
            }
            directoryAppendingPage_ = false;
        }
    }

    // --- server allocation ---
    {
        std::optional<core::ServerAllocation> allocation;
        {
            std::lock_guard<std::mutex> lock(allocationMutex_);
            if (allocationPendingResult_.has_value()) {
                allocation = std::move(allocationPendingResult_);
                allocationPendingResult_.reset();
            }
        }
        if (allocation.has_value()) {
            if (!allocation->success) {
                lastError_ = ShellErrorInfo{};
                lastError_.kind = ShellErrorKind::NetworkFailure;
                lastError_.detail = allocation->error;
                state_ = ShellState::Error;
            } else {
                lastJoinTicket_ = allocation->joinTicket;

                net::NetworkSession::Config config;
                config.mode = net::NetworkMode::Client;
                config.serverAddress = allocation->host;
                config.port = allocation->port;
                // Kronos ("via join tickets"): the real ticket the
                // backend just issued for THIS server travels in the join
                // handshake, where an allocated server validates it
                // against /v1/sessions/verify-ticket before admitting
                // anyone. Without this the allocation would be advisory
                // only -- a client could skip it and connect directly.
                config.joinTicket = allocation->joinTicket;

                ensureLocalProfileLoaded();
                app_.networkSession().setLocalDisplayName(localProfile_.displayName);
                app_.networkSession().setLocalIdentity(localProfile_.profileId, effectiveAgeGroup());

                if (!app_.startNetworking(config)) {
                    lastError_ = ShellErrorInfo{};
                    lastError_.kind = ShellErrorKind::NetworkFailure;
                    lastError_.detail = "Kronos allocated " + allocation->host + ":" +
                                        std::to_string(allocation->port) +
                                        " but the connection could not be started.";
                    state_ = ShellState::Error;
                } else {
                    lastJoinedHostDisplayName_ = allocationGameTitle_;
                    // Kronos ("Join Friend pathway"): real, own game id
                    // resolved from the catalogue already in memory --
                    // ServerAllocation itself carries no game id, only
                    // host/port/server_key (see its own comment). A
                    // slug not found in onlineGames_ (e.g. this was a
                    // local/offline join, not a real online allocation)
                    // leaves this empty, which presenceHeartbeat()
                    // already treats as "nothing to report".
                    onlineSessionGameId_.clear();
                    for (const core::CatalogueGame& game : onlineGames_) {
                        if (game.slug == allocationGameSlug_) {
                            onlineSessionGameId_ = game.id;
                            break;
                        }
                    }
                    onlineSessionServerKey_ = allocation->serverKey;
                    if (spawnNetworkedPlayerEntity_) {
                        core::EntityId entity = spawnNetworkedPlayerEntity_();
                        app_.setNetworkedLocalPlayerEntity(entity);
                    }
                    lanBrowser_.stop();
                    lanBrowserRunning_ = false;
                    lastError_ = ShellErrorInfo{};
                    state_ = computeNextState(state_, ShellEvent::JoinRequested);
                }
            }
        }
    }
}

void RuntimeShell::drawBackendAccountSection() {
    using namespace core::kronos_palette;
    // Kronos Client spec, Authentication Flow: "Absolutely no user or
    // password text fields ever appear in the launcher." The whole
    // email/password form that used to live here is gone -- the only
    // entry point is the green Sign In / Sign Up button in the top bar,
    // which hands off to the system browser. That is not just spec
    // compliance: a credential that never enters this process cannot be
    // captured from it.
    std::optional<core::KronosUser> user = kronosApi_.currentUser();
    if (user.has_value()) {
        std::string who = user->displayName.empty() ? user->email : user->displayName;
        ImGui::TextColored(paletteColor(kTextMuted), "Signed in as %s", who.c_str());
        if (!user->emailVerified) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.85f, 0.62f, 0.20f, 1.0f), "(email unconfirmed)");
        }
        return;
    }

    if (backendAuthInProgress_.load()) {
        ImGui::TextColored(paletteColor(kTextMuted), "%s", backendAuthStatusMessage_.c_str());
        return;
    }
    if (!backendAuthStatusMessage_.empty()) {
        ImGui::TextColored(ImVec4(0.85f, 0.35f, 0.30f, 1.0f), "%s", backendAuthStatusMessage_.c_str());
    }
    if (backendReachability_ == BackendReachability::Unreachable) {
        // Spec: an offline backend must say so, and must not imply that
        // local development is broken too -- it isn't.
        ImGui::TextColored(paletteColor(kTextMuted),
                           "Kronos services are unreachable. Local / Dev games under Create still work.");
    }
}

void RuntimeShell::drawOnlineCatalogueSection() {
    ImGui::SeparatorText("Kronos Online");

    // Browsing is public: the catalogue endpoint takes optional auth, so
    // a signed-out visitor still sees what is published. Only Play needs
    // an account, and that is enforced at allocation time.
    ImGui::BeginDisabled(catalogueFetchInProgress_.load());
    if (ImGui::SmallButton("Refresh##online")) {
        catalogueNextCursor_.clear();
        startCatalogueFetch(/*loadNextPage=*/false);
    }
    ImGui::EndDisabled();
    if (catalogueFetchInProgress_.load()) {
        ImGui::SameLine();
        ImGui::TextDisabled("Loading...");
    }

    if (!catalogueStatusMessage_.empty()) {
        ImGui::TextColored(ImVec4(0.8f, 0.25f, 0.2f, 1.0f), "%s", catalogueStatusMessage_.c_str());
    }
    if (onlineGames_.empty()) {
        if (catalogueStatusMessage_.empty()) ImGui::TextDisabled("No published games yet.");
        return;
    }

    // Real, live rows built entirely from the real backend response --
    // no placeholder entry is ever synthesized here.
    for (const core::CatalogueGame& game : onlineGames_) {
        ImGui::PushID(game.id.c_str());
        ImGui::BeginChild("##online_card", ImVec2(0.0f, 76.0f), true);

        ImGui::TextUnformatted(game.title.c_str());
        ImGui::TextDisabled("by %s", game.creatorName.c_str());

        // The real distinction the backend goes out of its way to make:
        // a real count versus genuinely not knowing. Never a fabricated
        // number, and never a confident 0 when the truth is "unknown".
        if (game.activePlayers < 0) {
            ImGui::TextDisabled("Players online: unavailable");
        } else if (game.activePlayers == 0) {
            ImGui::TextDisabled("No one playing right now");
        } else {
            ImGui::TextDisabled("%d playing now", game.activePlayers);
        }

        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 70.0f);
        ImGui::BeginDisabled(allocationInProgress_.load());
        pushPrimaryActionButtonColors();
        if (ImGui::Button("Play##online", ImVec2(70.0f, 0.0f))) startServerAllocation(game.slug, game.title);
        popPrimaryActionButtonColors();
        ImGui::EndDisabled();

        ImGui::EndChild();
        ImGui::PopID();
    }

    // Infinite scroll: pull the next batch once the user is within a
    // screenful of the end, so the next page is usually already there by
    // the time they reach it.
    if (!catalogueNextCursor_.empty() && !catalogueFetchInProgress_.load()) {
        const float scrollY = ImGui::GetScrollY();
        const float scrollMax = ImGui::GetScrollMaxY();
        if (scrollMax > 0.0f && scrollY >= scrollMax - ImGui::GetWindowHeight()) {
            startCatalogueFetch(/*loadNextPage=*/true);
        }
    }
    if (catalogueFetchInProgress_.load() && catalogueAppendingPage_) {
        ImGui::TextColored(paletteColor(core::kronos_palette::kTextMuted), "Loading more...");
    } else if (catalogueNextCursor_.empty() && onlineGames_.size() > kCatalogueBatchSize) {
        ImGui::TextColored(paletteColor(core::kronos_palette::kTextMuted), "That's everything (%zu games).", onlineGames_.size());
    }

    if (allocationInProgress_.load()) {
        ImGui::TextDisabled("Finding a server for %s...", allocationGameTitle_.c_str());
    }
}

void RuntimeShell::startUpdateCheck() {
    // Real, honest no-op: one check per session is enough, and a second
    // concurrent one would just race the first to the same answer.
    if (updateCheckInProgress_.load() || updateAvailable_) return;
    if (updateCheckThread_.joinable()) updateCheckThread_.join();

    updateCheckInProgress_.store(true);
    updateCheckThread_ = std::thread([this]() {
        core::UpdateCheckResult checkResult =
            core::checkForUpdate(core::kKronosVersion, kUpdateRepoOwner, kUpdateRepoName);
        std::lock_guard<std::mutex> lock(updateCheckResultMutex_);
        updateCheckPendingResult_ = std::move(checkResult);
        updateCheckInProgress_.store(false);
    });
}

void RuntimeShell::pollUpdateCheckResult() {
    std::optional<core::UpdateCheckResult> checkResult;
    {
        std::lock_guard<std::mutex> lock(updateCheckResultMutex_);
        if (!updateCheckPendingResult_.has_value()) return;
        checkResult = std::move(updateCheckPendingResult_);
        updateCheckPendingResult_.reset();
    }

    if (!checkResult->checked) {
        // Deliberately quiet for the user: a failed update check is a
        // real non-event for them (they're offline, GitHub is down,
        // they're rate limited). It goes to the log, never to a toast
        // that would interrupt someone who just wanted to play.
        core::logWarn("Update", "update check did not complete -- %s", checkResult->error.c_str());
        return;
    }
    if (!checkResult->updateAvailable) {
        core::logInfo("Update", "running %s; latest published release is %s -- already up to date.",
                      core::kKronosVersion, checkResult->latestTag.c_str());
        return;
    }
    core::logInfo("Update", "running %s; %s is available.", core::kKronosVersion, checkResult->latestTag.c_str());

    updateAvailable_ = true;
    updateAvailableTag_ = checkResult->latestTag;
    notify(core::NotificationKind::SystemMessage, "Update available",
           "Kronos " + checkResult->latestTag + " is available. You're on " + std::string(core::kKronosVersion) + ".");
}

bool RuntimeShell::startUpdateDownload() {
    // The real updater helper ships alongside the app's own executable,
    // so it is resolved relative to the real running binary rather than
    // the process's working directory (which is whatever the user
    // happened to launch from).
    std::filesystem::path exeDir(core::executableDirectory());
#if defined(_WIN32)
    std::filesystem::path helper = exeDir / "kronos_installer.exe";
    const char* relaunchExe = "engine_runtime.exe";
#else
    std::filesystem::path helper = exeDir / "kronos_installer";
    const char* relaunchExe = "engine_runtime";
#endif

    std::error_code ec;
    if (!std::filesystem::exists(helper, ec)) {
        updateStatusMessage_ = "The updater helper isn't installed next to Kronos -- download the new version from the "
                               "releases page instead.";
        return false;
    }

    std::vector<std::string> args = {"--update",
                                     "--install-dir",
                                     exeDir.string(),
                                     "--relaunch",
                                     relaunchExe,
                                     "--wait-pid",
                                     std::to_string(core::currentProcessId())};
    if (!core::launchProcess(helper.string(), args)) {
        updateStatusMessage_ = "Could not start the updater helper.";
        return false;
    }

    // The helper is now waiting on this process's own pid -- it cannot
    // safely touch a single file until this app is really gone, so the
    // only correct next step is to close.
    updateStatusMessage_ = "Kronos will close and reopen once the update is installed.";
    // Real shutdown through the app's own existing path: Window::pumpEvents()
    // already returns false on SDL_QUIT, so pushing a real quit event runs
    // the exact same ordered teardown a user clicking the window's close
    // button would -- rather than a second, parallel "please exit" flag
    // that every layer would then have to learn to respect.
    SDL_Event quitEvent;
    quitEvent.type = SDL_QUIT;
    SDL_PushEvent(&quitEvent);
    return true;
}

void RuntimeShell::startGoogleSignIn() {
    if (googleSignInInProgress_.load()) return; // real, honest no-op -- see this method's own header comment
    if (googleSignInThread_.joinable()) googleSignInThread_.join(); // real, previous background thread's own cleanup

    googleSignInInProgress_.store(true);
    googleSignInStatusMessage_ = "Opening your browser to sign in...";
    googleSignInThread_ = std::thread([this]() {
        core::GoogleAuthConfig config;
        core::GoogleAuthResult authResult = core::googleSignIn(config);
        std::lock_guard<std::mutex> lock(googleSignInResultMutex_);
        googleSignInPendingResult_ = std::move(authResult);
        googleSignInInProgress_.store(false);
    });
}

void RuntimeShell::pollGoogleSignInResult() {
    std::optional<core::GoogleAuthResult> result;
    {
        // Real, brief lock -- just to swap the real result out; nothing
        // that could block happens while holding it.
        std::lock_guard<std::mutex> lock(googleSignInResultMutex_);
        if (googleSignInPendingResult_.has_value()) result = std::move(googleSignInPendingResult_);
        googleSignInPendingResult_.reset();
    }
    if (!result.has_value()) return;

    if (!result->success) {
        googleSignInStatusMessage_ = "Sign-in failed: " + result->error;
        return;
    }

    localProfile_.googleSignedIn = true;
    localProfile_.googleSub = result->subject;
    localProfile_.googleEmail = result->email;
    localProfile_.googleDisplayName = result->displayName;

    // Kronos ("Google OAuth Authentication" -- "cache it securely"):
    // real, only the real OS-keychain half -- never LocalProfile.
    // Keyed by the real, stable googleSub (not email, which a real
    // account could change) so a later refresh always finds the right
    // real entry even if the display email is stale.
    bool storedRefreshToken =
        result->refreshToken.empty() ||
        core::storeCredential("google_oauth_refresh_token:" + result->subject, result->refreshToken);
    bool storedAccessToken = core::storeCredential("google_oauth_access_token:" + result->subject, result->accessToken);

    googleSignInStatusMessage_.clear();
    notify(core::NotificationKind::SystemMessage, "Signed in with Google",
           "Signed in as " + (result->displayName.empty() ? result->email : result->displayName) + ".");
    if (!storedRefreshToken || !storedAccessToken) {
        // Real, honest surfacing -- a failed secure-store isn't silently
        // swallowed (the user is signed in for *this* session, but a
        // real refresh next launch may not work) -- see
        // CredentialStore.hpp's own "real, honest best-effort" scope
        // note on why this can fail (locked keyring, no Secret Service
        // daemon running, etc.).
        std::fprintf(stderr, "RuntimeShell: could not securely store the real Google OAuth token(s) -- you may need "
                              "to sign in again next launch.\n");
    }
    (void)localProfile_.saveToFile(kLocalProfilePath);
}

void RuntimeShell::openFriends() {
    if (state_ != ShellState::Home) return;
    ensureLocalProfileLoaded();
    state_ = computeNextState(state_, ShellEvent::OpenFriends);
}

void RuntimeShell::drawFriendsPanel() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    // Inset into the shell chrome's content canvas -- drawing at full
    // viewport size here is what put this panel underneath the sidebar
    // and brand panel.
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + kSidebarWidth, viewport->WorkPos.y + kTopBarHeight));
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x - kSidebarWidth - kBrandPanelWidth,
                                     viewport->WorkSize.y - kTopBarHeight));
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
        social::FriendPresence presence = social::computeFriendPresence(
            friendEntry.displayName, lanBrowserRunning_ ? &lanBrowser_ : nullptr,
            app_.networkSession().isActive() ? &app_.networkSession() : nullptr);
        const char* presenceLabel =
            presence.state == social::PresenceState::InGame ? "In Game"
            : presence.state == social::PresenceState::Online ? "Online"
                                                                : "Offline";
        ImVec4 presenceColor = presence.state == social::PresenceState::InGame ? ImVec4(0.5f, 0.9f, 0.5f, 1.0f)
                                : presence.state == social::PresenceState::Online ? ImVec4(0.6f, 0.85f, 1.0f, 1.0f)
                                                                                   : ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
        ImGui::Text("%s", friendEntry.displayName.empty() ? friendEntry.friendId.c_str() : friendEntry.displayName.c_str());
        ImGui::SameLine();
        // Kronos ("Roblox-Style Presence" -- "status chips"): a real,
        // rounded status chip (not just bracketed text) -- background
        // tint from the same real presence color, so "what state is
        // this friend in" reads at a glance even before the label text
        // registers. `presence.detail` (a real session/game name, see
        // FriendPresence's own comment) appends onto the chip label
        // whenever the real underlying data actually has one.
        {
            std::string chipLabel = presenceLabel;
            if (!presence.detail.empty()) chipLabel += " -- " + presence.detail;
            ImVec2 textSize = ImGui::CalcTextSize(chipLabel.c_str());
            ImVec2 chipPos = ImGui::GetCursorScreenPos();
            ImVec2 chipPadding(8.0f, 3.0f);
            ImVec2 chipSize(textSize.x + chipPadding.x * 2.0f, textSize.y + chipPadding.y * 2.0f);
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->AddRectFilled(chipPos, ImVec2(chipPos.x + chipSize.x, chipPos.y + chipSize.y),
                                     ImGui::ColorConvertFloat4ToU32(ImVec4(presenceColor.x, presenceColor.y,
                                                                            presenceColor.z, 0.22f)),
                                     chipSize.y * 0.5f);
            ImGui::SetCursorScreenPos(ImVec2(chipPos.x + chipPadding.x, chipPos.y + chipPadding.y));
            ImGui::TextColored(presenceColor, "%s", chipLabel.c_str());
            ImGui::SetCursorScreenPos(chipPos);
            ImGui::Dummy(chipSize);
        }
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
    // Inset into the shell chrome's content canvas -- drawing at full
    // viewport size here is what put this panel underneath the sidebar
    // and brand panel.
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + kSidebarWidth, viewport->WorkPos.y + kTopBarHeight));
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x - kSidebarWidth - kBrandPanelWidth,
                                     viewport->WorkSize.y - kTopBarHeight));
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

    // Kronos ("Esc Pause Menu" fix -- live-reported issue: Escape used to
    // instantly disconnect the player): this whole panel used to render
    // unconditionally every InGame frame specifically so a "leave" action
    // was always reachable even while relative mouse mode kept the cursor
    // captured/hidden (see the real bug that prior fix solved). It's now
    // gated behind showPauseMenuOverlay_, toggled exclusively by tick()'s
    // own Escape handling -- that reachability guarantee still holds
    // because the toggle itself is keyboard-only, never requiring the
    // (possibly-captured) mouse to open the menu in the first place.
    if (!showPauseMenuOverlay_) return;

    ImGuiIO& io = ImGui::GetIO();

    // Kronos ("Esc Pause Menu" redesign): a real, distinctly-Kronos-
    // branded pause menu -- drawn from core::kronos_palette (the exact
    // same palette Home/Discover/Create already draw from, see
    // UITheme.hpp) and the real Kronos bold font, not a reskin of any
    // other platform's own pause-menu chrome. Functionally tabbed
    // (Players/Report) plus a real keyboard-shortcut row, replacing the
    // old flat always-on-top button strip.
    using namespace engine::core::kronos_palette;
    // Kronos ("Esc Pause Menu" -- live-reported issue: with the cursor
    // now genuinely free while this is open (see the Escape-toggle
    // block's own comment), a top-left-corner window sits far from
    // wherever a player's eye/cursor actually lands on open -- centered
    // is the real, standard placement for this kind of modal pause menu.
    ImVec2 pauseMenuCenter(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
    ImGui::SetNextWindowPos(pauseMenuCenter, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(480.0f, 0.0f), ImGuiCond_Appearing);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, paletteColor(kCharcoal));
    ImGui::PushStyleColor(ImGuiCol_Border, paletteColor(kSkyBlue));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.5f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    if (ImGui::Begin("##KronosPauseMenu", nullptr, ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoCollapse)) {
        ImFont* bold = core::kronosBoldFont();
        if (bold) ImGui::PushFont(bold);
        ImGui::TextColored(paletteColor(kTextBright), "MENU");
        if (bold) ImGui::PopFont();
        ImGui::SameLine();
        ImGui::TextColored(paletteColor(kTextMuted), "-- %s",
                            online ? (session.isServer() ? "Hosting" : "Connected") : "Offline");
        ImGui::Separator();

        // Quick-access row -- the exact same real overlays the old
        // always-visible HUD opened, unchanged in behavior, just no
        // longer drawn every frame (see this function's own header
        // comment on why hiding it behind Esc is still always reachable).
        if (ImGui::Button(online ? "Leave Session" : "Back to Home")) leaveSession();
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
        // Kronos ("Settings Panel v2 + Input Remapping + Accessibility
        // Layer" -- "reachable from Home and in-game pause menu"): real,
        // same overlay pattern as the "Shop" button just above.
        if (ImGui::Button("Graphics Settings")) {
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

        ImGui::Spacing();
        if (ImGui::BeginTabBar("##PauseMenuTabs")) {
            // showPlayerListOverlay_ doubles as a real, one-shot "open
            // straight to this tab" signal -- the real, scripted
            // ui.showPlayerList() entry point (see showPlayerList()'s own
            // comment) sets it before opening the menu; consumed (and
            // cleared, so it doesn't keep forcing this tab on later
            // manual switches) the moment this tab bar draws.
            ImGuiTabItemFlags playersTabFlags =
                showPlayerListOverlay_ ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
            showPlayerListOverlay_ = false;
            if (ImGui::BeginTabItem("Players", nullptr, playersTabFlags)) {
                if (!online) {
                    ImGui::TextColored(paletteColor(kTextMuted), "Playing offline -- no other players in this session.");
                } else {
                    ImGui::Text("Name: %s", session.sessionName().empty() ? "(unnamed)" : session.sessionName().c_str());
                    ImGui::Text("Session ID: %llu", static_cast<unsigned long long>(session.sessionId()));
                    ImGui::Text("Your role: %s", session.isServer() ? "Host" : "Guest");
                    if (!session.isServer()) {
                        ImGui::Text("Host: %s",
                                     lastJoinedHostDisplayName_.empty() ? "(unknown)" : lastJoinedHostDisplayName_.c_str());
                    }
                    ImGui::Separator();
                    // Kronos ("Active Joining UI"): a real, honest
                    // architectural note -- this engine's hosting process
                    // owns no PlayerId/roster entry of its own (only
                    // remote peers connecting IN get one, see
                    // net::NetworkSession::onPeerConnected()'s own
                    // comment), so there's no real per-player "is this one
                    // the host" flag to show client-side -- every entry a
                    // client sees really is a fellow guest. The host/guest
                    // distinction that DOES exist (this process's own
                    // role) is shown above instead of faking a per-player
                    // flag that doesn't correspond to anything real.
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
                ImGui::EndTabItem();
            }

            // Kronos ("In-Game Player Reporting"): real, player-facing
            // front door onto the exact same
            // net::NetworkSession::reportPlayer()/moderation::ReportLog
            // pipeline studio::plugins::ModerationPanel already exposes to
            // a moderator -- see that panel's own drawReportSection() for
            // the precedent this mirrors.
            if (ImGui::BeginTabItem("Report")) {
                ImGui::InputInt("Player ID", &reportTargetId_);
                const char* categories[] = {"Abuse", "Cheating", "Inappropriate Content"};
                ImGui::Combo("Category", &reportCategoryIndex_, categories, IM_ARRAYSIZE(categories));
                ImGui::InputTextMultiline("Description", reportDescriptionBuffer_, sizeof(reportDescriptionBuffer_),
                                           ImVec2(0.0f, 60.0f));
                ImGui::BeginDisabled(!session.isClient());
                pushPrimaryActionButtonColors();
                if (ImGui::Button("Submit Report")) {
                    auto category = static_cast<moderation::ReportCategory>(reportCategoryIndex_);
                    session.reportPlayer(static_cast<net::PlayerId>(reportTargetId_), category, reportDescriptionBuffer_);
                    reportStatus_ = "Report submitted.";
                    reportDescriptionBuffer_[0] = '\0';
                }
                popPrimaryActionButtonColors();
                ImGui::EndDisabled();
                if (!session.isClient()) ImGui::TextDisabled("Join a real multiplayer session to submit a report.");
                if (!reportStatus_.empty()) ImGui::TextDisabled("%s", reportStatus_.c_str());
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        // Kronos ("Esc Pause Menu" -- keyboard-shortcut row): real, live
        // hotkeys (see the L/R IsKeyPressed() checks below, outside this
        // window), shown as plain Kronos-palette chips -- deliberately
        // not a boxed-keycap widget styled after any other platform's own
        // pause-menu chrome (see this function's own header comment).
        ImGui::Spacing();
        ImGui::Separator();
        auto drawHotkeyHint = [](const char* key, const char* action) {
            ImGui::TextColored(ImVec4(kSkyBlue[0], kSkyBlue[1], kSkyBlue[2], kSkyBlue[3]), "%s", key);
            ImGui::SameLine(0.0f, 4.0f);
            ImGui::TextColored(ImVec4(kTextMuted[0], kTextMuted[1], kTextMuted[2], kTextMuted[3]), "%s", action);
        };
        drawHotkeyHint("Esc", "Resume");
        ImGui::SameLine(0.0f, 20.0f);
        drawHotkeyHint("L", "Leave Session");
        ImGui::SameLine(0.0f, 20.0f);
        drawHotkeyHint("R", "Reset Character");
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);

    // Kronos ("Esc Pause Menu" -- keyboard shortcuts): real, only live
    // while the menu itself is open (this whole function already early-
    // returned above otherwise) and no text field currently wants
    // keyboard input (the Report tab's own Player ID/Description fields)
    // -- same real "don't fire gameplay hotkeys while typing" convention
    // every other real keybind in this shell already follows (e.g.
    // tickEmoteActivation()'s own io.WantTextInput guard).
    if (!io.WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_L, false)) leaveSession();
        if (ImGui::IsKeyPressed(ImGuiKey_R, false)) {
            // Kronos ("Reset Character"): real, honest scope -- this
            // engine has no stored-per-game "spawn point" to return to
            // (spawnOfflinePlayerEntity_/spawnLocalPlayerAvatar() place a
            // new avatar once, at load time, but never remember that
            // position afterward), so this teleports the local player
            // straight up from their own current position instead of
            // faking a spawn marker that doesn't exist -- a real, honest
            // "get unstuck" respawn (falls back down clear of whatever
            // geometry they were stuck in/under), not a claimed "back to
            // spawn" that would sometimes be a lie.
            core::EntityId localEntity = app_.characterController().entity();
            if (const auto* transform = app_.ecs().tryGetComponent<core::Transform>(localEntity)) {
                (void)app_.respawnLocalPlayer(transform->position + glm::vec3(0.0f, 10.0f, 0.0f));
            }
        }
    }
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
