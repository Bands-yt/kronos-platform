#include "runtime/RuntimeShell.hpp"

#include <array>
#include <chrono>
#include <cstdio>
#include <vector>

#include <imgui.h>
#include <backends/imgui_impl_sdl2.h>
#include <backends/imgui_impl_vulkan.h>

#include "core/Components.hpp"

namespace engine::runtime {

namespace {
constexpr const char* kSessionHistoryPath = "session_history.sessions";
constexpr const char* kLocalProfilePath = "local_profile.profile";

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

RuntimeShell::RuntimeShell(core::Application& app, std::function<core::EntityId()> spawnNetworkedPlayerEntity)
    : app_(app), spawnNetworkedPlayerEntity_(std::move(spawnNetworkedPlayerEntity)) {}

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

    std::fprintf(stdout, "RuntimeShell: ImGui Vulkan backend initialized (dynamic rendering, format=%d)\n",
                 static_cast<int>(colorFormat));
    return true;
}

void RuntimeShell::shutdown() {
    lanBrowser_.stop();
    lanBrowserRunning_ = false;

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
    localProfile_ = core::loadOrCreateProfile(kLocalProfilePath);
    std::snprintf(displayNameBuffer_, sizeof(displayNameBuffer_), "%s", localProfile_.displayName.c_str());
    localProfileLoaded_ = true;
}

void RuntimeShell::ensureSessionHistoryLoaded() {
    if (sessionHistoryLoaded_) return;
    (void)sessionHistory_.loadFromFile(kSessionHistoryPath);
    sessionHistoryLoaded_ = true;
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
    if (state_ != ShellState::SessionBrowser) return; // real, honest no-op outside the real state this applies to

    ensureLocalProfileLoaded();

    net::NetworkSession::Config config;
    config.mode = net::NetworkMode::Client;
    config.serverAddress = session.sourceAddress;
    config.port = session.gamePort;
    lastJoinedHostDisplayName_ = session.hostDisplayName;

    app_.networkSession().setLocalDisplayName(localProfile_.displayName);
    if (!app_.startNetworking(config)) {
        lastError_ = ShellErrorInfo{};
        lastError_.kind = ShellErrorKind::NetworkFailure;
        lastError_.detail = "Failed to start networking for " + session.sourceAddress;
        state_ = ShellState::Error;
        return;
    }

    ensureSessionHistoryLoaded();
    int64_t now =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    sessionHistory_.recordConnection(session.sessionName, session.sourceAddress, session.gamePort, now);
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
    state_ = computeNextState(state_, ShellEvent::SessionEnded);
}

void RuntimeShell::playOffline() {
    if (state_ != ShellState::Home) return;
    app_.input().setRelativeMouseMode(true);
    state_ = computeNextState(state_, ShellEvent::PlayOffline);
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
    // (playOffline() never touches networkSession() at all).
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

    beginFrame();
    switch (state_) {
        case ShellState::Home: drawHomePanel(); break;
        case ShellState::SessionBrowser: drawSessionBrowserPanel(); break;
        case ShellState::Loading: drawLoadingPanel(); break;
        case ShellState::Error: drawErrorPanel(); break;
        case ShellState::InGame: drawPlayerListOverlay(); break;
    }
    endFrame();
}

void RuntimeShell::drawHomePanel() {
    ensureLocalProfileLoaded();

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin("Kronos", nullptr,
                  ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);

    ImGui::SetCursorPos(ImVec2(viewport->WorkSize.x * 0.5f - 160.0f, viewport->WorkSize.y * 0.25f));
    ImGui::BeginGroup();
    ImGui::Text("KRONOS"); // real, honest default font -- no custom font asset shipped for this shell yet
    ImGui::Spacing();

    if (ImGui::InputText("Playing As", displayNameBuffer_, sizeof(displayNameBuffer_))) {
        localProfile_.displayName = displayNameBuffer_;
        (void)localProfile_.saveToFile(kLocalProfilePath);
    }
    ImGui::Spacing();
    ImGui::Spacing();

    ImVec2 buttonSize(200.0f, 40.0f);
    if (ImGui::Button("Play", buttonSize)) playOffline();
    if (ImGui::Button("Sessions", buttonSize)) showSessionBrowser();
    // Kronos ("Active Joining UI"): Create/Plugins/Assets are honestly
    // NOT this player client's job -- engine_runtime has no scene editor,
    // no plugin browser, no asset browser of its own (that's Kronos
    // Studio, a separate binary/creator tool). Real, honest disabled
    // buttons with an explanatory tooltip beat either faking the
    // functionality here or silently omitting the nav items the original
    // checklist explicitly named.
    ImGui::BeginDisabled(true);
    ImGui::Button("Create", buttonSize);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Open Kronos Studio to create or edit a project.");
    ImGui::BeginDisabled(true);
    ImGui::Button("Plugins", buttonSize);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Open Kronos Studio's Plugin Browser to manage plugins.");
    ImGui::BeginDisabled(true);
    ImGui::Button("Assets", buttonSize);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Open Kronos Studio's Asset Browser to manage assets.");
    ImGui::EndGroup();

    ImGui::End();
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
    std::vector<net::DiscoveredSession> discovered = lanBrowser_.discoveredSessions();
    if (discovered.empty()) {
        ImGui::TextDisabled("Searching for real sessions being announced on your LAN...");
    } else {
        if (ImGui::BeginTable("DiscoveredSessions", 5,
                               ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Name");
            ImGui::TableSetupColumn("Host");
            ImGui::TableSetupColumn("Players");
            ImGui::TableSetupColumn("Ping");
            ImGui::TableSetupColumn("");
            ImGui::TableHeadersRow();
            for (const auto& session : discovered) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%s", session.sessionName.empty() ? "(unnamed session)" : session.sessionName.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%s", session.hostDisplayName.c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%u / %u", session.currentPlayerCount, session.maxPlayerCount);
                ImGui::TableSetColumnIndex(3);
                if (session.pingMs > 0.0f) {
                    ImGui::Text("%.0f ms", static_cast<double>(session.pingMs));
                } else {
                    ImGui::TextDisabled("--");
                }
                ImGui::TableSetColumnIndex(4);
                ImGui::PushID(static_cast<int>(session.sessionId));
                if (ImGui::SmallButton("Join")) joinSession(session);
                ImGui::PopID();
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

    ImGui::SeparatorText("Join by address");
    ImGui::InputText("Address", manualAddressBuffer_, sizeof(manualAddressBuffer_));
    ImGui::InputInt("Port", &manualPortValue_);
    manualPortValue_ = manualPortValue_ < 1 ? 1 : (manualPortValue_ > 65535 ? 65535 : manualPortValue_);
    if (ImGui::Button("Join Address")) {
        net::DiscoveredSession manual;
        manual.sourceAddress = manualAddressBuffer_;
        manual.gamePort = static_cast<uint16_t>(manualPortValue_);
        joinSession(manual);
    }

    ImGui::End();
}

void RuntimeShell::drawLoadingPanel() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin("Connecting", nullptr,
                  ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);

    ImGui::SetCursorPos(ImVec2(viewport->WorkSize.x * 0.5f - 100.0f, viewport->WorkSize.y * 0.45f));
    ImGui::BeginGroup();
    ImGui::Text("Connecting to %s:%u...", manualAddressBuffer_, static_cast<unsigned>(manualPortValue_));
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
    if (!session.isActive()) return; // real, honest no-op for offline play -- nothing to show

    // Always-visible, minimal HUD -- a real "Leave Session" action must
    // always be reachable while InGame, not hidden behind a toggle.
    ImGui::SetNextWindowPos(ImVec2(16.0f, 16.0f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.65f);
    if (ImGui::Begin("##InGameHud", nullptr,
                      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoNav |
                          ImGuiWindowFlags_NoFocusOnAppearing)) {
        ImGui::Text("%s", session.isServer() ? "Hosting" : "Connected");
        if (ImGui::Button("Leave Session")) leaveSession();
        ImGui::SameLine();
        if (ImGui::Button(showPlayerListOverlay_ ? "Hide Players" : "Show Players")) {
            showPlayerListOverlay_ = !showPlayerListOverlay_;
        }
    }
    ImGui::End();

    if (!showPlayerListOverlay_) return;

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

} // namespace engine::runtime
