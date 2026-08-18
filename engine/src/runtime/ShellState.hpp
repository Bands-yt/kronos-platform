#pragma once

#include <cstdint>
#include <string>

#include "net/NetworkSession.hpp"

namespace engine::runtime {

// Kronos ("Active Joining UI" -- engine_runtime ImGui + input
// integration): the real states engine_runtime's new pre-game shell can
// be in. Home/SessionBrowser/Loading/GameCatalogue/Error are all real
// menu states (mouse visible/absolute, ImGui panels drawn); InGame is
// the existing, unchanged playable-scene state every CLI mode already
// had before this shell existed (mouse captured/relative, no menu
// panels drawn).
//
// GameCatalogue (Kronos "Game Catalogue Overhaul") is the real
// replacement for the old bare "Play" button -- picking a
// ProjectPath-kind game there real-loads it via runtime::loadGame()
// (core/GameManifest.hpp) and enters InGame the same way a successful
// join does; picking a CliFlag-kind game (TNT Wars/Mining Sim/House
// Demo, still hardcoded C++ gameplay, not scene data) instead relaunches
// engine_runtime itself via core::launchProcess() and never touches this
// state machine at all.
// Kronos ("Marketplace" -- "engine_runtime-side catalogue UI"): AvatarShop
// is the real player-facing equivalent of studio::plugins::CataloguePanel
// -- browse the same real, shared avatar-item catalogue
// (core::CatalogueIndex/core::CatalogueDatabase), equip owned items, and
// spend real KronosCredits, all from inside engine_runtime itself. Before
// this, the entire real Marketplace/economy system (built across this
// codebase's history) was only ever reachable from Studio -- a real,
// honest, previously-stated gap (see studio::plugins::CataloguePanel's
// own now-stale class comment) that this closes.
// Kronos ("Settings Panel v2 + Input Remapping + Accessibility Layer"):
// Settings is real, reachable from Home (like AvatarShop) -- the in-game
// pause-menu path is a real, separate overlay flag (mirroring
// RuntimeShell::showAvatarShopOverlay_'s own real precedent), not a
// second ShellState, since a real game is still live underneath it.
// Kronos ("Social Layer" -- "Friends + Presence + Messaging" /
// "Notifications System"): Friends and Notifications are real, reachable
// from Home, same dual "ShellState + in-game overlay flag" shape
// AvatarShop/Settings already establish -- see
// RuntimeShell::showFriendsOverlay_/showNotificationsOverlay_'s own
// comments for the in-game half.
enum class ShellState {
    Home,
    SessionBrowser,
    Loading,
    GameCatalogue,
    AvatarShop,
    Settings,
    Friends,
    Notifications,
    InGame,
    Error,
};

// The real, honest reasons a state transition into Error happened --
// mirrors net::JoinFailureReason/net::DisconnectReason (see
// net/NetworkSession.hpp) rather than inventing a second notion of
// "what went wrong."
enum class ShellErrorKind {
    None,
    JoinFailed,     // net::JoinFailureReason -- see ShellErrorInfo::joinFailureReason
    Disconnected,   // net::DisconnectReason -- see ShellErrorInfo::disconnectReason
    NetworkFailure, // startNetworking()/initialize() itself returned false (e.g. real socket bind failure)
};

struct ShellErrorInfo {
    ShellErrorKind kind = ShellErrorKind::None;
    net::JoinFailureReason joinFailureReason = net::JoinFailureReason::None;
    uint32_t joinFailureServerProtocolVersion = 0;
    net::DisconnectReason disconnectReason = net::DisconnectReason::None;
    std::string detail; // real, free-text elaboration (e.g. the address that failed to connect)
};

// The real, discrete things that can happen while in the shell -- the
// pure input half of the state machine (see computeNextState() below).
// Deliberately small and flat, not a full event-bus type: this is a real,
// linear menu flow (Home -> browse -> join -> playing -> leave -> Home),
// not a general-purpose UI framework.
enum class ShellEvent {
    OpenSessionBrowser, // Home -> SessionBrowser
    ReturnHome,         // SessionBrowser/GameCatalogue/Error -> Home
    JoinRequested,       // SessionBrowser -> Loading (a real join attempt just started)
    JoinSucceeded,       // Loading -> InGame
    JoinFailed,          // Loading -> Error
    CancelJoin,          // Loading -> Home (the player backed out of a real, still-in-flight join attempt)
    OpenGameCatalogue,   // Home -> GameCatalogue (Kronos "Game Catalogue Overhaul" -- replaces the old bare Play button)
    GameSelected,        // GameCatalogue -> Loading (Kronos "Animated Hourglass Loading Screen": a game was picked,
                         // real runtime::loadGame() work is about to start -- used to go straight to InGame,
                         // see RuntimeShell::finishPendingGameLoad()'s own comment for why a real Loading beat
                         // was inserted)
    GameLoadFinished,    // Loading -> InGame (the deferred runtime::loadGame() from GameSelected above real-succeeded)
    GameLoadFailed,      // Loading -> GameCatalogue (the deferred runtime::loadGame() real-failed -- matches the
                         // real, original pre-Loading-beat behavior of just staying on the Catalogue, not a
                         // formal Error panel; a local game-load failure isn't a network problem)
    SessionEnded,        // InGame -> Home (a real, graceful or ungraceful disconnect/leave)
    OpenAvatarShop,      // Home -> AvatarShop
    OpenSettings,        // Home -> Settings
    OpenFriends,         // Home -> Friends
    OpenNotifications,   // Home -> Notifications
};

// Kronos ("Active Joining UI"): the real, pure state-transition function
// -- deliberately extracted from RuntimeShell's own ImGui/window-
// dependent drawing code so it's headlessly testable (no live window/GPU
// needed to verify the real state machine's own logic is correct, same
// "GPU/window code gets structural verification, pure logic gets full
// coverage" split this whole test suite already follows throughout).
// A transition this function doesn't recognize for the given `current`
// state is a real, honest no-op (returns `current` unchanged) rather
// than an assertion/crash -- a stray/duplicate event (e.g. a double
// click) should never corrupt shell state.
[[nodiscard]] inline ShellState computeNextState(ShellState current, ShellEvent event) {
    switch (current) {
        case ShellState::Home:
            if (event == ShellEvent::OpenSessionBrowser) return ShellState::SessionBrowser;
            if (event == ShellEvent::OpenGameCatalogue) return ShellState::GameCatalogue;
            if (event == ShellEvent::OpenAvatarShop) return ShellState::AvatarShop;
            if (event == ShellEvent::OpenSettings) return ShellState::Settings;
            if (event == ShellEvent::OpenFriends) return ShellState::Friends;
            if (event == ShellEvent::OpenNotifications) return ShellState::Notifications;
            return current;
        case ShellState::SessionBrowser:
            if (event == ShellEvent::ReturnHome) return ShellState::Home;
            if (event == ShellEvent::JoinRequested) return ShellState::Loading;
            return current;
        case ShellState::Loading:
            if (event == ShellEvent::JoinSucceeded) return ShellState::InGame;
            if (event == ShellEvent::JoinFailed) return ShellState::Error;
            if (event == ShellEvent::CancelJoin) return ShellState::Home;
            // Kronos ("Animated Hourglass Loading Screen"): real, same
            // Loading state a network join already uses -- a real local
            // game load now shows the same real loading beat instead of
            // jumping straight from GameCatalogue to InGame.
            if (event == ShellEvent::GameLoadFinished) return ShellState::InGame;
            if (event == ShellEvent::GameLoadFailed) return ShellState::GameCatalogue;
            return current;
        case ShellState::GameCatalogue:
            if (event == ShellEvent::ReturnHome) return ShellState::Home;
            // Kronos ("Animated Hourglass Loading Screen"): real, changed
            // from a direct -> InGame jump -- see
            // RuntimeShell::selectGame()'s own comment.
            if (event == ShellEvent::GameSelected) return ShellState::Loading;
            // Kronos ("Merged Game Catalogue & Sessions View"): real,
            // same rule SessionBrowser already has above -- a card's own
            // expanded live-session list can now real-join directly from
            // GameCatalogue (see RuntimeShell::joinSession()'s own
            // relaxed guard), not just from the standalone Session
            // Browser panel.
            if (event == ShellEvent::JoinRequested) return ShellState::Loading;
            return current;
        case ShellState::AvatarShop:
            if (event == ShellEvent::ReturnHome) return ShellState::Home;
            return current;
        case ShellState::Settings:
            if (event == ShellEvent::ReturnHome) return ShellState::Home;
            return current;
        case ShellState::Friends:
            if (event == ShellEvent::ReturnHome) return ShellState::Home;
            return current;
        case ShellState::Notifications:
            if (event == ShellEvent::ReturnHome) return ShellState::Home;
            return current;
        case ShellState::InGame:
            if (event == ShellEvent::SessionEnded) return ShellState::Home;
            return current;
        case ShellState::Error:
            if (event == ShellEvent::ReturnHome) return ShellState::Home;
            return current;
    }
    return current;
}

} // namespace engine::runtime
