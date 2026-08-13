#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "core/ScriptShellController.hpp"

struct lua_State;

namespace engine::core {

class UIRenderer;

// Kronos (Alpha Roadmap Phase 7, "Lua Scripting Platform" -- "Lua UI"):
// a real, minimal Luau binding for core::UIRenderer -- the same
// deliberately original, flat-function-table shape ScriptWorldApi/
// ScriptNetworkApi already established (`ui.drawText(...)`, not a
// Roblox-style ScreenGui/Frame/TextLabel Instance tree -- that declarative
// widget-tree surface is the real, separate, substantial feature
// studio::plugins::ScriptedPlugin's own header comment already flags as
// deliberately out of scope for "Plugin UI access"; this is the smaller,
// honest thing that's actually buildable here: direct screen-space draw
// calls, matching what UIRenderer itself already offers native C++ HUD
// code).
//
// Real queue, not a direct pass-through: UIRenderer::beginFrame() clears
// its own batch and is called once per real frame from inside
// core::Application's own HUD code (camera-showcase mode and the live
// TNT Wars match HUD, see Application.cpp's own two beginFrame() call
// sites) -- but Luau scripts run during the *sim* tick, earlier in the
// frame. Calling UIRenderer::drawText() directly from a script would
// just get wiped out by the later beginFrame() call. So ui.drawText()/
// ui.drawRect() here only enqueue; flushInto() (called by Application
// right after each real beginFrame()) replays the queue into the real
// UIRenderer batch, then clears it -- a script that wants to keep
// something on screen calls ui.drawText() again every tick, the same
// "draw every frame or it doesn't render" contract every other
// UIRenderer call site already has.
//
// Honest, real scope boundary: this only actually renders anything in
// the two real contexts Application.cpp already calls
// UIRenderer::beginFrame() from (camera-showcase mode, an active TNT
// Wars match). A general, always-on HUD pass for every other scene/mode
// is a separate, pre-existing architectural gap (UIRenderer itself has
// no unconditional per-frame drive today) -- not something this binding
// creates or claims to fix.
class ScriptUiApi {
public:
    void registerInto(lua_State* L);

    // Real flush -- called by core::Application right after each of its
    // own real UIRenderer::beginFrame() calls, so queued script draws
    // land in that same frame's batch before UIRenderer::draw() submits
    // it. Clears the queue afterward.
    void flushInto(UIRenderer& uiRenderer);

    // Kronos ("Active Joining UI" -- Lua UI bindings): wires
    // ui.sessionBrowser()/ui.playerList()/ui.joinSession(id)/
    // ui.leaveSession() to a real engine_runtime shell -- see
    // ScriptShellController's own comment. nullptr (the default) makes
    // all four real, honest no-ops on call, matching this codebase's
    // established "an unset handler is a no-op, not an error" convention
    // (net::RemoteEvent::handleFromClient()'s own precedent) -- Studio
    // (and any other embedder with nothing to show a session browser for)
    // never calls this and the bindings still exist, they just do
    // nothing.
    void setShellController(ScriptShellController* controller) { shellController_ = controller; }

private:
    static int luaDrawText(lua_State* L);
    static int luaDrawRect(lua_State* L);
    static int luaSessionBrowser(lua_State* L);
    static int luaPlayerList(lua_State* L);
    static int luaJoinSession(lua_State* L);
    static int luaLeaveSession(lua_State* L);

    struct TextCommand {
        std::string text;
        glm::vec2 position{0.0f};
        float scale = 1.0f;
        glm::vec4 color{1.0f};
    };
    struct RectCommand {
        glm::vec2 position{0.0f};
        glm::vec2 size{0.0f};
        glm::vec4 color{1.0f};
    };

    std::vector<TextCommand> textCommands_;
    std::vector<RectCommand> rectCommands_;
    ScriptShellController* shellController_ = nullptr;
};

} // namespace engine::core
