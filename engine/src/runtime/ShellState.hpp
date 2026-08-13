#pragma once

#include <cstdint>
#include <string>

#include "net/NetworkSession.hpp"

namespace engine::runtime {

// Kronos ("Active Joining UI" -- engine_runtime ImGui + input
// integration): the real states engine_runtime's new pre-game shell can
// be in. Home/SessionBrowser/Loading/Error are all real menu states
// (mouse visible/absolute, ImGui panels drawn); InGame is the existing,
// unchanged playable-scene state every CLI mode already had before this
// shell existed (mouse captured/relative, no menu panels drawn).
enum class ShellState {
    Home,
    SessionBrowser,
    Loading,
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
    ReturnHome,         // SessionBrowser/Error -> Home
    JoinRequested,       // SessionBrowser -> Loading (a real join attempt just started)
    JoinSucceeded,       // Loading -> InGame
    JoinFailed,          // Loading -> Error
    CancelJoin,          // Loading -> Home (the player backed out of a real, still-in-flight join attempt)
    PlayOffline,         // Home -> InGame (no networking at all -- today's existing default behavior)
    SessionEnded,        // InGame -> Home (a real, graceful or ungraceful disconnect/leave)
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
            if (event == ShellEvent::PlayOffline) return ShellState::InGame;
            return current;
        case ShellState::SessionBrowser:
            if (event == ShellEvent::ReturnHome) return ShellState::Home;
            if (event == ShellEvent::JoinRequested) return ShellState::Loading;
            return current;
        case ShellState::Loading:
            if (event == ShellEvent::JoinSucceeded) return ShellState::InGame;
            if (event == ShellEvent::JoinFailed) return ShellState::Error;
            if (event == ShellEvent::CancelJoin) return ShellState::Home;
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
