#pragma once

#include <cstdint>

namespace engine::core {

// Kronos ("Active Joining UI" -- Lua UI bindings): the real seam between
// core::ScriptUiApi's four session-related bindings
// (ui.sessionBrowser()/ui.playerList()/ui.joinSession(id)/ui.leaveSession())
// and engine_runtime's own real Home Screen shell (runtime::RuntimeShell,
// which implements this). ScriptUiApi lives in shared engine_core
// (Studio's static scene-editing ECS has no live joinable session to
// show a browser for), while these four bindings are only ever
// meaningful in engine_runtime -- this small abstract interface keeps
// engine_runtime-only concrete code out of the shared engine_core
// library, the same "orchestration class doesn't hardcode gameplay
// logic, an injected seam does" pattern net::NetworkSession::
// setOnPlayerJoin() already establishes elsewhere in this codebase.
//
// Every method here is real, honest, and idempotent from a script's
// point of view -- calling joinSession() while already InGame, or
// leaveSession() while at the Home Screen, is a real no-op on the
// implementing side (see RuntimeShell's own methods), not an error a
// script has to guard against itself.
class ScriptShellController {
public:
    virtual ~ScriptShellController() = default;

    virtual void showSessionBrowser() = 0;
    virtual void showPlayerList() = 0;
    virtual void joinSession(uint64_t sessionId) = 0;
    virtual void leaveSession() = 0;
};

} // namespace engine::core
