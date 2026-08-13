#pragma once

#include <functional>
#include <string>

#include "core/Application.hpp"
#include "core/LocalProfile.hpp"
#include "core/ScriptShellController.hpp"
#include "net/LanSessionBrowser.hpp"
#include "net/SessionHistory.hpp"
#include "runtime/ShellState.hpp"

struct VkDescriptorPool_T;
using VkDescriptorPool = VkDescriptorPool_T*;
struct ImDrawData;

namespace engine::runtime {

// Kronos ("Active Joining UI"): engine_runtime's new pre-game shell -- a
// real Home Screen / Session Browser / Loading / Error UI, built on Dear
// ImGui exactly the way studio::StudioApp already proves out (see that
// class's own initImGuiVulkanBackend()/beginFrame()/endFrame(), which
// this replicates almost verbatim -- confirmed to have zero
// Studio-specific coupling: core::Renderer::setOverlayCallback() and
// core::Window::setRawEventCallback() are both already generic
// engine_core API). Only ever constructed for the plain, no-flag launch
// path (see main.cpp's own homeScreenMode gating) -- every other CLI mode
// (--server/--client/--trailer/--miningsim/--render-showcase/--tntwars)
// never touches this at all, so ImGui/LAN-discovery overhead is zero for
// those paths.
//
// Owns the one real state machine (ShellState, see that header for the
// pure transition logic this class drives) and the one real join/leave
// code path -- both the native ImGui buttons this class draws AND the
// Lua ui.joinSession()/ui.leaveSession() bindings (Phase 6's
// ScriptShellController implementation) call the exact same
// joinSession()/leaveSession() methods below, not two independently-
// drifting notions of "join a session."
// Kronos ("Active Joining UI" -- Lua UI bindings): implements
// core::ScriptShellController so a real Luau script's ui.sessionBrowser()/
// ui.playerList()/ui.joinSession(id)/ui.leaveSession() calls (wired
// through core::ScriptUiApi::setShellController(), see main.cpp) drive
// the exact same real methods this shell's own native ImGui buttons call
// -- one real join/leave code path, not two independently-drifting ones.
class RuntimeShell : public core::ScriptShellController {
public:
    // `app` must outlive this shell (main.cpp constructs both and owns
    // this shell for exactly as long as app.run() blocks).
    // `spawnNetworkedPlayerEntity` is the same "caller supplies gameplay
    // logic via an injected callback" pattern net::NetworkSession::
    // setOnPlayerJoin() already establishes -- main.cpp is the only place
    // that actually knows how to build a real networked-player entity
    // (which mesh, what capsule size); this shell just needs the real
    // EntityId back to hand to app.setNetworkedLocalPlayerEntity().
    RuntimeShell(core::Application& app, std::function<core::EntityId()> spawnNetworkedPlayerEntity);
    ~RuntimeShell();

    RuntimeShell(const RuntimeShell&) = delete;
    RuntimeShell& operator=(const RuntimeShell&) = delete;

    // Real ImGui+Vulkan backend init/shutdown -- see class comment.
    // initialize() must be called after app.renderer() (and therefore
    // app.window()) are already real and initialized; the caller is
    // expected to bail out of the whole process on a real `false` here,
    // same as every other initialize()-returns-bool contract in this
    // codebase.
    [[nodiscard]] bool initialize();
    void shutdown();

    [[nodiscard]] ShellState state() const { return state_; }

    // The real, once-per-frame drive -- registered as GameLoop's own
    // PreRenderHook by main.cpp (see that hook's own comment for why
    // ImGui's per-frame sequence needs to run here, before renderFrame()
    // reaches the overlay callback). Polls real NetworkSession state for
    // the real Loading->InGame/Error and InGame->Error (on a real
    // disconnect) transitions, ticks the real LAN browser while it's
    // running, then draws whatever panel the current real state calls
    // for.
    void tick(float dt);

    // Kronos ("Active Joining UI" -- Lua UI bindings, Phase 6): the real
    // entry points ui.sessionBrowser()/ui.joinSession(id)/
    // ui.leaveSession() forward to -- see ScriptShellController (added in
    // Phase 6) for how a Luau script reaches these. Real, honest no-ops
    // when called from a state they don't apply to (e.g. joinSession()
    // while already InGame), matching this codebase's "an inapplicable
    // call is a no-op, not an error" convention throughout.
    // core::ScriptShellController overrides -- see that interface's own
    // comment. joinSession(uint64_t) looks the id up among
    // lanBrowser_.discoveredSessions() and, if found, forwards to the
    // real joinSession(const DiscoveredSession&) overload below -- the
    // exact same real join path the native Session Browser panel's own
    // "Join" button uses, not a second one.
    void showSessionBrowser() override;
    void showPlayerList() override;
    void joinSession(uint64_t sessionId) override;
    void leaveSession() override;

    // The real join entry point every other real trigger (the native
    // Session Browser panel's "Join" button, "Recently played" rows,
    // manual address entry, and joinSession(uint64_t) above) ultimately
    // calls -- one real code path.
    void joinSession(const net::DiscoveredSession& session);
    void cancelJoin();
    void playOffline();

private:
    void beginFrame();
    void endFrame();
    void tickLanBrowserIfNeeded(float dt);
    void ensureLocalProfileLoaded();
    void ensureSessionHistoryLoaded();

    void drawHomePanel();
    void drawSessionBrowserPanel();
    void drawLoadingPanel();
    void drawErrorPanel();
    void drawPlayerListOverlay();

    core::Application& app_;
    std::function<core::EntityId()> spawnNetworkedPlayerEntity_;

    ShellState state_ = ShellState::Home;
    ShellErrorInfo lastError_;

    net::LanSessionBrowser lanBrowser_;
    float lanBrowserClock_ = 0.0f;
    bool lanBrowserRunning_ = false;

    net::SessionHistory sessionHistory_;
    bool sessionHistoryLoaded_ = false;
    core::LocalProfile localProfile_;
    bool localProfileLoaded_ = false;
    char displayNameBuffer_[64] = "";

    char manualAddressBuffer_[128] = "127.0.0.1";
    int manualPortValue_ = 7777;

    // Kronos ("Active Joining UI" -- Session Metadata Panel, "host
    // profile"): captured from the real DiscoveredSession at the moment
    // joinSession() is called -- once actually connected, the roster
    // (net::NetworkSession::clientKnownPlayers()) has no entry for the
    // host at all (see net::NetworkSession::onPeerConnected()'s own
    // comment -- the hosting process owns no PlayerId of its own), so
    // this is the one real place a client can still show who's hosting.
    std::string lastJoinedHostDisplayName_;

    bool showPlayerListOverlay_ = false;

    VkDescriptorPool imguiDescriptorPool_ = nullptr;
    ImDrawData* pendingDrawData_ = nullptr;
};

} // namespace engine::runtime
