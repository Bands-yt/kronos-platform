#pragma once

#include "core/UILayout.hpp"

struct lua_State;

namespace engine::core {

class Scripting;
class UIRenderer;

// Kronos ("Native Vector UI Engine" -- v0.4.0 Creator Suite): the real
// Luau binding for core::UILayoutTree -- a declarative sibling to
// ScriptUiApi.hpp's immediate-mode `ui.drawText()/ui.drawRect()`
// surface (see that header's own comment on why this engine deliberately
// doesn't have a Roblox-style Instance/Frame widget tree yet). This one
// owns exactly one real UILayoutTree per instance and exposes it as a
// flat `uiLayout.*` function table -- the same original shape
// ScriptWorldApi/ScriptNetworkApi/ScriptUiApi already establish, not a
// second design language.
//
// Bindings (`uiLayout.bindValue/bindText/bindColor`) wrap a Luau
// function exactly the way Scripting::invokeCallback() already calls
// `events.onX` handlers (lua_ref the function, lua_pcall it, refresh the
// VM's watchdog deadline first via Scripting::refreshWatchdogDeadline --
// the same real requirement core::ScriptNetworkApi's own async handler
// calls already document) -- called once per frame from renderInto(),
// which runs from Application's own render step, not from inside
// Scripting::tick()'s resume path, matching ScriptNetworkApi's own
// "async relative to the sim tick" call shape.
class ScriptUiLayoutApi {
public:
    explicit ScriptUiLayoutApi(Scripting& scripting) : scripting_(scripting) {}

    void registerInto(lua_State* L);

    // Real per-frame drive: resolves every live binding, computes flex
    // layout against `availableSize`, and draws the whole tree into
    // `uiRenderer`'s current batch at `screenOffset`. Call once per
    // frame, the same "or it doesn't render" contract every other
    // UIRenderer-driven surface in this engine already has.
    void renderInto(UIRenderer& uiRenderer, glm::vec2 availableSize, glm::vec2 screenOffset = glm::vec2(0.0f));

    [[nodiscard]] UILayoutTree& tree() { return tree_; }

private:
    static int luaNode(lua_State* L);
    static int luaRemove(lua_State* L);
    static int luaSetParent(lua_State* L);
    static int luaSetRoot(lua_State* L);
    static int luaSetDirection(lua_State* L);
    static int luaSetJustify(lua_State* L);
    static int luaSetAlign(lua_State* L);
    static int luaSetSize(lua_State* L);
    static int luaSetFlex(lua_State* L);
    static int luaSetGap(lua_State* L);
    static int luaSetPadding(lua_State* L);
    static int luaSetMargin(lua_State* L);
    static int luaSetColor(lua_State* L);
    static int luaSetColorEnd(lua_State* L);
    static int luaSetFillFromValue(lua_State* L);
    static int luaSetText(lua_State* L);
    static int luaSetTextScale(lua_State* L);
    static int luaBindValue(lua_State* L);
    static int luaBindText(lua_State* L);
    static int luaBindColor(lua_State* L);

    // Real, ref-counted Luau function call helpers shared by the three
    // bindX entry points -- see luaBindValue()'s own .cpp comment for
    // why the returned std::function closes over a ref-lifetime holder
    // instead of leaking the lua_ref() for as long as the VM lives.
    float callNumber(lua_State* owner, int ref, float fallback);
    std::string callString(lua_State* owner, int ref, const std::string& fallback);
    glm::vec4 callColor(lua_State* owner, int ref, glm::vec4 fallback);

    Scripting& scripting_;
    UILayoutTree tree_;
};

} // namespace engine::core
