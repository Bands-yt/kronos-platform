#pragma once

#include <functional>
#include <string>

#include <glm/glm.hpp>
#include <imgui.h>

#include "core/AnimationDatabase.hpp"
#include "core/Application.hpp"
#include "core/CatalogueDatabase.hpp"
#include "analytics/TelemetryQueue.hpp"
#include "analytics/TelemetrySender.hpp"
#include "core/GameCatalogueAggregate.hpp"
#include "core/LocalGameDirectory.hpp"
#include "core/LocalProfile.hpp"
#include "core/RiggedAvatar.hpp"
#include "core/ScriptShellController.hpp"
#include "marketplace/TransactionLog.hpp"
#include "net/GamePlayLog.hpp"
#include "net/LanSessionBrowser.hpp"
#include "net/SessionBrowserSort.hpp"
#include "net/SessionHistory.hpp"
#include "notification/NotificationService.hpp"
#include "runtime/HomeAvatarPreview.hpp"
#include "runtime/ShellState.hpp"
#include "social/FriendsService.hpp"

struct VkDescriptorPool_T;
using VkDescriptorPool = VkDescriptorPool_T*;
struct ImDrawData;

namespace engine::runtime {

// Kronos ("Active Joining UI"): engine_runtime's new pre-game shell -- a
// real Home Screen / Session Browser / Game Catalogue / Loading / Error
// UI, built on Dear
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
    // Kronos ("Kronos Player & Chat System Initialization" -- "Spawn a
    // default avatar when entering any world"): a real, second, separate
    // callback for the genuinely different offline/local spawn path --
    // unlike a networked join (a bare Transform+Renderable entity;
    // net::NetworkSession's own ClientPrediction drives its movement),
    // launching a ProjectPath game from the Game Catalogue needs a real,
    // physics-driven core::CharacterController::spawn() (gravity,
    // collision, WASD/mouse-look), the same real character every CLI
    // launch mode already gets -- runtime::loadGame() itself only
    // rebuilds the scene's own static content (core::SceneManager::
    // loadScene() has no concept of "the player"), so nothing spawned a
    // controllable avatar at all until this callback's real call site
    // (selectGame(), below) was added.
    // Kronos ("Avatar Phase" -- "AvatarEditor: Skin-Tone Selection" /
    // "Avatar Head System" / "Body Sliders" / "Clothing & Accessory
    // Slots"): `spawnOfflinePlayerEntity`'s real signature grew
    // `glm::vec4 skinTone`/`core::HeadShape headShape`/
    // `core::BodyProportions bodyProportions`/`core::AvatarLoadout
    // loadout`/`core::CatalogueIndex catalogueIndex` parameters -- this
    // shell is the one real caller that knows the local player's own
    // real, chosen appearance (localProfile_.skinToneIndex/headShapeIndex/
    // bodyHeight/bodyWidth/bodyLimbScale plus the real, on-disk avatar
    // loadout/catalogue -- see selectGame()'s own real call site and
    // ensureAvatarCatalogueLoaded()), main.cpp's real spawn lambda just
    // forwards whatever it's given to
    // core::Application::spawnLocalPlayerAvatar().
    RuntimeShell(core::Application& app, std::function<core::EntityId()> spawnNetworkedPlayerEntity,
                 std::function<core::EntityId(glm::vec4 skinTone, core::HeadShape headShape,
                                               core::BodyProportions bodyProportions, const core::AvatarLoadout& loadout,
                                               const core::CatalogueIndex& catalogueIndex,
                                               const core::AnimationOverrides& animationOverrides)>
                     spawnOfflinePlayerEntity);
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
    // Session Browser panel's "Join" button and "Recently played" rows)
    // ultimately calls -- one real code path.
    void joinSession(const net::DiscoveredSession& session);
    void cancelJoin();

    // Kronos ("Game Catalogue Overhaul"): the real replacement for the
    // old bare playOffline()/"Play" button. openGameCatalogue()
    // real-builds core::buildGameCatalogueEntries() (games/, resolved
    // the same packaged-vs-dev-build way as assets/templates/examples,
    // see ENGINE_GAMES_DIR -- ranked by real QualityScore against the
    // real local play log) and enters GameCatalogue; selectGame() is the
    // one real code path the native Featured/genre/Hidden-Gems row UI
    // (Phase 5) calls -- ProjectPath games load via runtime::loadGame()
    // and enter InGame, CliFlag games instead relaunch engine_runtime
    // itself via core::launchProcess() and never touch this state
    // machine.
    void openGameCatalogue();
    void selectGame(const core::GameCatalogueEntry& game);

    // Opens the real, separate `studio` binary (core::launchProcess(),
    // found as a real sibling of this running executable via
    // core::executableDirectory()) -- does not exit engine_runtime, since
    // a solo creator plausibly wants both running at once.
    void launchStudio();

    // Kronos ("Moderation Architecture v2", "Tester Safety Mode"): a
    // real, pre-launch safety default for an unverified tester build --
    // "unverified" here is honest, not a real identity check (no real
    // tester-verification system exists anywhere in this codebase, same
    // "no auth" scope core::LocalProfile already states for accounts in
    // general); this is a real, explicit `--tester-mode` CLI launch
    // configuration (main.cpp), not a fabricated automatic detection.
    // Defaults false so every existing launch is unaffected. See
    // effectiveAgeGroup()'s own comment for how this one flag real-forces
    // every one of the spec's four named behaviors (MinorMode/SafeChat/
    // SafeDM/SafeGamesOnly) through the exact same AgeGroup-gated
    // machinery already built for Minor Mode Enforcement, rather than
    // four separate new mechanisms.
    void setTesterSafetyMode(bool enabled) { testerSafetyMode_ = enabled; }

private:
    // The real AgeGroup every Minor-Mode-gated decision in this class
    // (Catalogue filtering, session-join blocking, DM/chat restriction
    // via setLocalIdentity) should actually use -- testerSafetyMode_
    // forces Minor regardless of the real, self-declared
    // localProfile_.ageGroup, closing "force MinorMode for unverified
    // testers" and, for free, SafeChat/SafeDM/SafeGamesOnly too: those
    // three are not separate systems, they're what Minor Mode already
    // means everywhere else in this codebase (safety::PolicyEngine's
    // stricter thresholds, NetworkSession's DM restriction, this class's
    // own Catalogue/Session-Browser filtering) -- forcing the one real
    // AgeGroup input forces all of them consistently, with no risk of
    // the four drifting out of sync the way four independent flags could.
    [[nodiscard]] core::AgeGroup effectiveAgeGroup() const {
        return testerSafetyMode_ ? core::AgeGroup::Minor : localProfile_.ageGroup;
    }

    void beginFrame();
    void endFrame();
    void tickLanBrowserIfNeeded(float dt);
    void ensureLocalProfileLoaded();
    void ensureSessionHistoryLoaded();
    void ensureGamePlayLogLoaded();
    // Kronos ("Avatar Phase" -- "AvatarEditor: Clothing & Accessory
    // Slots"): real, lazy, same "load once, real file may not exist yet"
    // shape as ensureLocalProfileLoaded() -- loads the same real
    // "catalogue.json"/"local_avatar_loadout.loadout" files
    // studio::StudioApp's own catalogueDatabase_/catalogueIndex_/
    // localAvatarLoadout_ use, so a Catalogue game launched from
    // engine_runtime shows the real, same equipped items Studio's
    // AvatarEditor last saved -- one real, shared record, not two
    // independently-drifting ones.
    void ensureAvatarCatalogueLoaded();
    // Kronos ("Home Screen Avatar Preview"): real, lazy, same "load once,
    // real GPU resources acquired on first real use" shape
    // ensureAvatarCatalogueLoaded() already establishes -- constructed
    // only the first time Home is actually drawn (never for a CLI mode
    // that skips the Home Screen entirely).
    void ensureHomeAvatarPreviewLoaded();

    void drawHomePanel();
    void drawSessionBrowserPanel();
    void drawLoadingPanel();
    void drawErrorPanel();
    void drawGameCataloguePanel();
    void drawPlayerListOverlay();
    // Kronos ("Marketplace" -- "engine_runtime-side catalogue UI"): real
    // -- the one real entry point Home's own "Avatar Shop" button calls.
    // Same "load the shared data once, on a real, deliberate user action"
    // shape openGameCatalogue() already establishes.
    void openAvatarShop();
    void drawAvatarShopPanel();
    void drawAvatarShopDetailPopup();
    // Kronos ("Player & Chat System" -- chat panel): tickChatActivation()
    // checks the real "/" key press (only while chat isn't already open,
    // and only while no other real ImGui text field already wants
    // keyboard text -- see its own .cpp comment) and opens the panel;
    // drawChatPanel() draws the real input box + real message history
    // (fed by net::NetworkSession::setOnChatMessageReceived(), see
    // initialize()'s own real callback registration) and handles real
    // Enter-to-send/Escape-to-close.
    void tickChatActivation();
    void drawChatPanel();

    // Kronos ("Settings Panel v2 + Input Remapping + Accessibility
    // Layer"): real -- same dual "Home state + in-game overlay" shape
    // AvatarShop already establishes (see showAvatarShopOverlay_'s own
    // comment).
    void openSettings();
    void drawSettingsPanel();
    // Kronos ("Settings Panel v2 + Input Remapping + Accessibility
    // Layer" -- "Settings applied immediately"): real -- called once at
    // startup (right after the real profile first loads) and again every
    // time a setting actually changes in drawSettingsPanel(), so a
    // freshly-launched session already reflects a real, previously-saved
    // profile's own settings, not just the engine's own hardcoded
    // defaults until the player opens Settings once.
    void applyAllSettingsFromProfile();
    // Kronos ("Settings Panel v2 + Input Remapping + Accessibility
    // Layer" -- "Graphics: Quality presets"): real -- composes
    // core::Renderer's own real, already-existing per-feature toggles
    // (there is no existing tiered-preset abstraction to extend, see
    // this method's own .cpp comment). Low = performance mode (disables
    // RT shadows/cinematic mode as a real side effect) + every other
    // heavy toggle off. Medium = SSR/volumetric fog on, RT
    // reflections/RT GI/cinematic mode off. High = cinematic mode on
    // (disables performance mode as a real side effect) + every toggle
    // on. Out-of-range values real-clamp to Medium.
    void applyQualityPreset(int presetIndex);
    // Kronos ("Input Remapping System"): real -- clears then re-applies
    // every one of kRemappableActions' own real bindings from
    // localProfile_.inputBindingOverrides (falling back to that action's
    // own real, already-existing default binding when no override is
    // stored -- see LocalProfile::inputBindingOverrides' own comment).
    void applyInputBindingOverrides();

    // Kronos ("Social Layer" -- "Friends + Presence + Messaging"): real,
    // same dual "Home state + in-game overlay" shape AvatarShop/Settings
    // already establish (see showFriendsOverlay_'s own comment).
    void openFriends();
    void drawFriendsPanel();

    // Kronos ("Notifications System"): real -- same dual-reachability
    // shape as openFriends()/drawFriendsPanel() above.
    void openNotifications();
    void drawNotificationsPanel();
    // The one real place a notification is both persisted (via
    // notification::push()) and queued as a transient toast -- every
    // real producer in this class (purchases, friend requests,
    // moderation updates) calls this instead of duplicating both steps.
    // Does NOT itself save localProfile_ -- callers already save after
    // their own real mutation, this just appends to the same in-memory
    // profile first so the save picks it up too.
    void notify(core::NotificationKind kind, std::string title, std::string body, std::string relatedId = std::string());
    // Real, small, top-right stack of transient toast popups -- ticked
    // down every real frame in tick(), drawn every frame regardless of
    // ShellState (a purchase/friend-request toast should be visible
    // whether the player is on Home, InGame, or anywhere else).
    void tickToasts(float dt);
    void drawToasts();

    core::Application& app_;
    std::function<core::EntityId()> spawnNetworkedPlayerEntity_;
    std::function<core::EntityId(glm::vec4 skinTone, core::HeadShape headShape, core::BodyProportions bodyProportions,
                                  const core::AvatarLoadout& loadout, const core::CatalogueIndex& catalogueIndex,
                                  const core::AnimationOverrides& animationOverrides)>
        spawnOfflinePlayerEntity_;

    ShellState state_ = ShellState::Home;
    ShellErrorInfo lastError_;

    net::LanSessionBrowser lanBrowser_;
    float lanBrowserClock_ = 0.0f;
    bool lanBrowserRunning_ = false;

    // Kronos ("Session Browser Polish v2"): real sort/filter UI state --
    // see net::SessionSortOrder/net::filterDiscoveredSessionsToFriends()
    // for the real, pure logic these index into.
    int sessionBrowserSortIndex_ = 0;
    bool sessionBrowserFriendsOnly_ = false;

    net::SessionHistory sessionHistory_;
    bool sessionHistoryLoaded_ = false;
    core::LocalProfile localProfile_;
    bool testerSafetyMode_ = false;
    bool localProfileLoaded_ = false;
    char displayNameBuffer_[64] = "";

    // Kronos ("Avatar Phase" -- "AvatarEditor: Clothing & Accessory
    // Slots" / "Animation Overrides"): see ensureAvatarCatalogueLoaded()'s
    // own comment. animationDatabase_ is the real, same "animations.json"
    // database studio::plugins::UploadAnimationPlugin writes to, needed
    // to resolve localProfile_'s own animOverride*Id fields into real
    // clip file paths.
    core::CatalogueDatabase avatarCatalogueDatabase_;
    core::CatalogueIndex avatarCatalogueIndex_;
    core::AvatarLoadout avatarLoadout_;
    core::AnimationDatabase animationDatabase_;
    bool avatarCatalogueLoaded_ = false;
    // Kronos ("Marketplace" -- "engine_runtime-side catalogue UI"): real,
    // same "transaction_log.transactions" file studio::StudioApp's own
    // transactionLog_ uses -- one real, shared purchase/publish ledger
    // across Studio and engine_runtime, not two independently-drifting
    // ones.
    marketplace::TransactionLog transactionLog_;

    // Kronos ("Simple Recommendation Engine" -- "Track CTR and
    // conversion for tuning; log data for future ML"): real, local,
    // disk-persisted telemetry -- see analytics::TelemetrySender's own
    // header comment for exactly what "real" means here (a real,
    // structured local log; no real network backend exists to actually
    // ship it anywhere, same honest "local Alpha substitute" framing
    // net::GamePlayLog already establishes). telemetryQueue_ must be
    // declared before telemetrySender_ (member-initializer-list order
    // follows declaration order).
    analytics::TelemetryQueue telemetryQueue_;
    analytics::TelemetrySender telemetrySender_{telemetryQueue_};
    float telemetryFlushClock_ = 0.0f;

    // Kronos ("Home Screen Avatar Preview"): real, lazily constructed --
    // see ensureHomeAvatarPreviewLoaded()'s own comment. nullptr until
    // Home has actually been drawn once.
    std::unique_ptr<HomeAvatarPreview> homeAvatarPreview_;

    // Kronos ("Marketplace" -- "engine_runtime-side catalogue UI"): real
    // Avatar Shop UI state -- same shape studio::plugins::CataloguePanel's
    // own searchText_/categoryFilterIndex_/sortOrderIndex_/detailItemId_
    // already establish, reused here rather than inventing a second
    // design for the same real browsing task.
    char avatarShopSearchText_[128] = "";
    int avatarShopCategoryFilterIndex_ = 0;
    int avatarShopSortOrderIndex_ = 0;
    std::string avatarShopDetailItemId_;
    bool avatarShopDetailOpen_ = false;
    std::string avatarShopStatusMessage_;
    // Kronos ("Marketplace" -- "engine_runtime-side catalogue UI" --
    // live re-equip while InGame): real -- a separate real flag from the
    // ShellState::AvatarShop the Home button drives; the "Shop" HUD
    // button drawn during InGame (see drawPlayerListOverlay()) toggles
    // this instead of the state machine, the same "drawn as a real
    // overlay on top of gameplay, not a separate menu state" precedent
    // showChatPanel_ already establishes. drawAvatarShopPanel()'s own
    // "Back" button checks this to know whether to close the overlay or
    // real-transition ShellState back to Home.
    bool showAvatarShopOverlay_ = false;

    // Kronos ("Settings Panel v2 + Input Remapping + Accessibility
    // Layer"): same real dual "Home state + in-game overlay" shape as
    // showAvatarShopOverlay_ above.
    bool showSettingsOverlay_ = false;
    // Kronos ("Input Remapping System"): real key-capture state -- empty
    // = not currently rebinding anything; non-empty = the real action
    // name currently "listening" for the next real key press (see
    // drawSettingsPanel()'s own Controls section). Real, live SDL
    // keyboard-state polling, not an ImGui-specific hook -- see that
    // method's own comment on why.
    std::string rebindingActionName_;
    // Kronos ("Settings Panel v2 + Input Remapping + Accessibility
    // Layer" -- "UI scale must affect all panels"): a real snapshot of
    // ImGui's own style, captured once right after core::
    // applyKronosUITheme() runs in initialize() -- ImGui::GetStyle().
    // ScaleAllSizes() is real but cumulative (each call multiplies
    // *current* sizes), so re-scaling on every settings change re-derives
    // from this fixed real baseline instead of compounding.
    ImGuiStyle baseUIStyle_;

    // Kronos ("Social Layer"): same real dual "Home state + in-game
    // overlay" shape showAvatarShopOverlay_ already establishes.
    bool showFriendsOverlay_ = false;
    char addFriendIdBuffer_[64] = "";
    char addFriendNameBuffer_[64] = "";
    std::string friendsStatusMessage_;
    // Empty = no conversation open; otherwise the real friendId of the
    // friend whose messages the messaging overlay (drawn inside
    // drawFriendsPanel()) is currently showing.
    std::string openConversationFriendId_;
    char friendMessageInputBuffer_[256] = "";

    // Kronos ("Notifications System"): same real dual-reachability shape.
    bool showNotificationsOverlay_ = false;
    // -1 = "All" -- indexes core::NotificationKind otherwise, same
    // "index 0 reserved for the unfiltered/Any choice" convention this
    // shell's own avatarShopCategoryFilterIndex_ already uses.
    int notificationFilterKindIndex_ = -1;
    bool notificationUnreadOnlyFilter_ = false;

    // Kronos ("Home UI Polish" -- "Smooth transitions"): real, general
    // (applies to every ShellState panel, not just Home) fade-in --
    // tick() resets stateTransitionClock_ to 0 the real frame `state_`
    // itself changes, then ramps a real ImGuiStyleVar_Alpha push from 0
    // to 1 over kStateTransitionFadeSeconds. Toasts are pushed/popped
    // outside this alpha scope (see tick()'s own real ordering) so an
    // in-flight toast never fades with the panel underneath it.
    ShellState previousDrawState_ = ShellState::Home;
    float stateTransitionClock_ = 0.0f;
    static constexpr float kStateTransitionFadeSeconds = 0.25f;

    // Real, transient toast popup queue -- see notify()'s own comment.
    struct ToastEntry {
        std::string title;
        std::string body;
        float remainingSeconds = 0.0f;
    };
    static constexpr float kToastDurationSeconds = 4.0f;
    std::vector<ToastEntry> activeToasts_;

    // Kronos ("Trailer Capture Mode"): real, InGame-only capture-support
    // overlay -- deliberately distinct from trailer::TrailerDirector (the
    // separate, TNT-Wars-specific, deterministic --trailer CLI pipeline
    // that renders offline frame sequences to disk). This is a live,
    // interactive "clean up what's on screen while I play" aid for a
    // real, ordinary in-game session -- toggled by a real, raw SDL key
    // (F9), same "raw SDL, not a remappable UnifiedInput action" reason
    // rebindingActionName_'s own key-capture polling already uses (a
    // meta/dev toggle, not a gameplay action). "No video export" (the
    // spec's own words) -- see drawTrailerCapturePanel()'s own comment.
    void tickTrailerCaptureMode(float dt);
    void drawTrailerCapturePanel();
    bool trailerCaptureModeEnabled_ = false;
    bool trailerHudHidden_ = false;
    bool trailerF9WasDown_ = false;
    bool trailerF10WasDown_ = false;
    // Real camera smoothing -- exponential approach toward whatever pose
    // gameplay wrote to app_.camera() this tick, applied only while
    // trailerCaptureModeEnabled_ is true (zero effect otherwise, so
    // ordinary play is completely unchanged). Reset to "no smoothing
    // applied yet" whenever capture mode is (re-)enabled, so the first
    // frame never smooths from a stale pose.
    bool trailerSmoothedPoseValid_ = false;
    core::Camera trailerSmoothedCamera_;
    float trailerCameraSmoothingSeconds_ = 0.15f;

    // Real, named camera poses a player can save/recall while in
    // Trailer Capture Mode -- pure, in-memory (session-local; not
    // persisted, since a bookmark only makes sense against the specific
    // real scene/session it was captured in).
    struct CameraBookmark {
        std::string name;
        core::Camera pose;
    };
    std::vector<CameraBookmark> trailerBookmarks_;
    char trailerBookmarkNameBuffer_[48] = "";

    // Real, named bundles of the above settings -- see
    // drawTrailerCapturePanel()'s own comment for exactly what each
    // preset sets. Deliberately does NOT touch window resolution/
    // fullscreen -- core::Window has no runtime mode-switch API, same
    // real, already-stated gap Settings' own qualityPresetIndex comment
    // documents.
    int trailerExportPresetIndex_ = 0;

    // Kronos ("Branding + Release Prep"): a real, brief, one-time splash
    // (shown for kSplashDurationSeconds the very first time this shell
    // ever reaches Home, never again after) -- see drawSplashPanel()'s
    // own comment for why this is a text wordmark, not an image.
    bool showSplash_ = true;
    float splashClock_ = 0.0f;
    static constexpr float kSplashDurationSeconds = 1.6f;
    void drawSplashPanel();

    // Real, Home-reachable "About" overlay -- version/build info, same
    // simple bool-overlay shape this class already uses throughout.
    bool showAboutOverlay_ = false;
    void drawAboutPanel();

    // Kronos ("Game Catalogue Overhaul", Phase 4): scanned once on
    // openGameCatalogue() (real disk I/O -- not re-scanned every frame),
    // not while sitting on Home. Real, honest empty vector if games/
    // doesn't exist or has nothing in it, same "missing input is a real
    // zero-result answer" convention scanLocalGameDirectory() itself
    // already establishes.
    std::vector<core::GameCatalogueEntry> discoveredGames_;
    bool gamesScanned_ = false;

    // Kronos ("Game Catalogue Overhaul", Phase 6): the real, local,
    // persisted retention-data source core::computeGamePlayStats()/
    // computeQualityScore() (core/QualityScore.hpp) read from. Lazily
    // loaded the same way sessionHistory_ is; `currentGameId_` tracks
    // which real game session (if any) selectGame() opened, so
    // leaveSession() knows what to real-close on the way back to Home --
    // empty means no local (ProjectPath) game session is currently open
    // (e.g. this shell is in a real networked session instead, which
    // isn't tracked here at all).
    net::GamePlayLog gamePlayLog_;
    bool gamePlayLogLoaded_ = false;
    std::string currentGameId_;

    // Kronos ("Active Joining UI" -- Session Metadata Panel, "host
    // profile"): captured from the real DiscoveredSession at the moment
    // joinSession() is called -- once actually connected, the roster
    // (net::NetworkSession::clientKnownPlayers()) has no entry for the
    // host at all (see net::NetworkSession::onPeerConnected()'s own
    // comment -- the hosting process owns no PlayerId of its own), so
    // this is the one real place a client can still show who's hosting.
    std::string lastJoinedHostDisplayName_;

    bool showPlayerListOverlay_ = false;

    // Kronos ("Player & Chat System" -- chat panel): real, bounded
    // (kMaxChatHistoryLines) history -- same real "don't grow unbounded"
    // reasoning moderation::ChatLog's own ring-buffer already applies,
    // here for local *display* purposes only (the real, unbounded,
    // moderator-facing record is moderation::ChatLog on the server, not
    // this client-side display list). Each entry is already formatted
    // ("Name: text") at append time, not re-resolved at draw time --
    // resolving a sender's display name from net::NetworkSession::
    // clientKnownPlayers() after that player has since left would
    // silently lose their name, so it's captured once, live, when the
    // message actually arrives.
    static constexpr size_t kMaxChatHistoryLines = 100;
    std::vector<std::string> chatHistoryLines_;
    bool showChatPanel_ = false;
    bool chatPanelJustOpened_ = false; // real, one-frame "grab keyboard focus" signal for the input box below
    char chatInputBuffer_[256] = "";

    // Kronos ("Active Joining UI" -- real bug fix): edge-detection state
    // for the "ToggleMenu" (Escape) action, same
    // isActionDown()+"WasDown_" pattern core::Application already uses
    // for every other keyboard toggle. Without this, InGame (reached via
    // the Home Screen's own Game Catalogue/"Join" -- not just a CLI flag) had no
    // way back out at all once the mouse was captured: no keyboard
    // shortcut existed, and the always-visible HUD's own buttons were
    // themselves unreachable by a hidden, captured cursor.
    bool escapeKeyWasDown_ = false;
    // Kronos ("Input Remapping System"): real edge-detect state for the
    // real "OpenChat" bound action, same shape as escapeKeyWasDown_ just
    // above -- platform_adapters::UnifiedInput::isActionDown() reports
    // per-frame held state, not a "just pressed" edge, so tickChatActivation()
    // tracks the previous frame's value itself.
    bool openChatKeyWasDown_ = false;

    VkDescriptorPool imguiDescriptorPool_ = nullptr;
    ImDrawData* pendingDrawData_ = nullptr;
};

} // namespace engine::runtime
