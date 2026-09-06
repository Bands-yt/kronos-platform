#include "core/ScriptUiLayoutApi.hpp"

#include <cstdio>
#include <cstring>
#include <memory>

#include <lua.h>
#include <lualib.h>

#include "core/Scripting.hpp"
#include "core/UIRenderer.hpp"

namespace engine::core {

namespace {

ScriptUiLayoutApi* selfFromUpvalue(lua_State* L) {
    return static_cast<ScriptUiLayoutApi*>(lua_tolightuserdata(L, lua_upvalueindex(1)));
}

FlexDirection parseDirection(const char* s) { return (std::strcmp(s, "column") == 0) ? FlexDirection::Column : FlexDirection::Row; }

JustifyContent parseJustify(const char* s) {
    if (std::strcmp(s, "center") == 0) return JustifyContent::Center;
    if (std::strcmp(s, "end") == 0) return JustifyContent::End;
    if (std::strcmp(s, "between") == 0) return JustifyContent::SpaceBetween;
    if (std::strcmp(s, "around") == 0) return JustifyContent::SpaceAround;
    return JustifyContent::Start;
}

AlignItems parseAlign(const char* s) {
    if (std::strcmp(s, "center") == 0) return AlignItems::Center;
    if (std::strcmp(s, "end") == 0) return AlignItems::End;
    if (std::strcmp(s, "start") == 0) return AlignItems::Start;
    return AlignItems::Stretch;
}

UISizeMode parseSizeMode(const char* s) {
    if (std::strcmp(s, "fixed") == 0) return UISizeMode::Fixed;
    if (std::strcmp(s, "percent") == 0) return UISizeMode::Percent;
    return UISizeMode::Auto;
}

UINodeKind parseKind(const char* s) {
    if (std::strcmp(s, "rect") == 0) return UINodeKind::Rect;
    if (std::strcmp(s, "text") == 0) return UINodeKind::Text;
    return UINodeKind::Container;
}

} // namespace

float ScriptUiLayoutApi::callNumber(lua_State* owner, int ref, float fallback) {
    scripting_.refreshWatchdogDeadline(owner);
    lua_getref(owner, ref);
    if (lua_pcall(owner, 0, 1, 0) != LUA_OK) {
        std::fprintf(stderr, "[luau] uiLayout.bindValue callback error: %s\n", lua_tostring(owner, -1));
        lua_pop(owner, 1);
        return fallback;
    }
    float result = static_cast<float>(lua_tonumber(owner, -1));
    lua_pop(owner, 1);
    return result;
}

std::string ScriptUiLayoutApi::callString(lua_State* owner, int ref, const std::string& fallback) {
    scripting_.refreshWatchdogDeadline(owner);
    lua_getref(owner, ref);
    if (lua_pcall(owner, 0, 1, 0) != LUA_OK) {
        std::fprintf(stderr, "[luau] uiLayout.bindText callback error: %s\n", lua_tostring(owner, -1));
        lua_pop(owner, 1);
        return fallback;
    }
    const char* s = lua_tostring(owner, -1);
    std::string result = s ? s : fallback;
    lua_pop(owner, 1);
    return result;
}

glm::vec4 ScriptUiLayoutApi::callColor(lua_State* owner, int ref, glm::vec4 fallback) {
    scripting_.refreshWatchdogDeadline(owner);
    lua_getref(owner, ref);
    if (lua_pcall(owner, 0, 1, 0) != LUA_OK) {
        std::fprintf(stderr, "[luau] uiLayout.bindColor callback error: %s\n", lua_tostring(owner, -1));
        lua_pop(owner, 1);
        return fallback;
    }
    // Real, honest scope: expects a real 4-element array table {r,g,b,a}
    // -- the same flat-table color shape ScriptUiApi's own drawText/
    // drawRect arguments use component-wise. Any other return shape
    // (wrong type, short table) yields `fallback` for the missing
    // components rather than raising -- an out-of-band Lua error from a
    // per-frame binding would otherwise spam stderr every single frame.
    glm::vec4 result = fallback;
    if (lua_istable(owner, -1)) {
        for (int i = 0; i < 4; ++i) {
            lua_rawgeti(owner, -1, i + 1);
            if (lua_isnumber(owner, -1)) result[i] = static_cast<float>(lua_tonumber(owner, -1));
            lua_pop(owner, 1);
        }
    }
    lua_pop(owner, 1);
    return result;
}

int ScriptUiLayoutApi::luaNode(lua_State* L) {
    const char* kind = luaL_optstring(L, 1, "container");
    ScriptUiLayoutApi* self = selfFromUpvalue(L);
    int id = self->tree_.addNode(parseKind(kind));
    lua_pushinteger(L, id);
    return 1;
}

int ScriptUiLayoutApi::luaRemove(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    selfFromUpvalue(L)->tree_.removeNode(id);
    return 0;
}

int ScriptUiLayoutApi::luaSetParent(lua_State* L) {
    int childId = static_cast<int>(luaL_checkinteger(L, 1));
    int parentId = static_cast<int>(luaL_optinteger(L, 2, 0));
    selfFromUpvalue(L)->tree_.setParent(childId, parentId);
    return 0;
}

int ScriptUiLayoutApi::luaSetRoot(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    selfFromUpvalue(L)->tree_.setRoot(id);
    return 0;
}

int ScriptUiLayoutApi::luaSetDirection(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    const char* dir = luaL_checkstring(L, 2);
    if (UINode* node = selfFromUpvalue(L)->tree_.findNode(id)) node->direction = parseDirection(dir);
    return 0;
}

int ScriptUiLayoutApi::luaSetJustify(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    const char* v = luaL_checkstring(L, 2);
    if (UINode* node = selfFromUpvalue(L)->tree_.findNode(id)) node->justify = parseJustify(v);
    return 0;
}

int ScriptUiLayoutApi::luaSetAlign(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    const char* v = luaL_checkstring(L, 2);
    if (UINode* node = selfFromUpvalue(L)->tree_.findNode(id)) node->align = parseAlign(v);
    return 0;
}

// uiLayout.setSize(id, "width"|"height", "auto"|"fixed"|"percent", value)
int ScriptUiLayoutApi::luaSetSize(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    const char* axis = luaL_checkstring(L, 2);
    const char* mode = luaL_checkstring(L, 3);
    float value = static_cast<float>(luaL_optnumber(L, 4, 0.0));
    if (UINode* node = selfFromUpvalue(L)->tree_.findNode(id)) {
        UISize size{parseSizeMode(mode), value};
        if (std::strcmp(axis, "height") == 0) node->height = size; else node->width = size;
    }
    return 0;
}

int ScriptUiLayoutApi::luaSetFlex(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    float grow = static_cast<float>(luaL_optnumber(L, 2, 0.0));
    float shrink = static_cast<float>(luaL_optnumber(L, 3, 1.0));
    if (UINode* node = selfFromUpvalue(L)->tree_.findNode(id)) {
        node->flexGrow = grow;
        node->flexShrink = shrink;
    }
    return 0;
}

int ScriptUiLayoutApi::luaSetGap(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    float gap = static_cast<float>(luaL_checknumber(L, 2));
    if (UINode* node = selfFromUpvalue(L)->tree_.findNode(id)) node->gap = gap;
    return 0;
}

int ScriptUiLayoutApi::luaSetPadding(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    float l = static_cast<float>(luaL_checknumber(L, 2));
    float t = static_cast<float>(luaL_checknumber(L, 3));
    float r = static_cast<float>(luaL_checknumber(L, 4));
    float b = static_cast<float>(luaL_checknumber(L, 5));
    if (UINode* node = selfFromUpvalue(L)->tree_.findNode(id)) {
        node->paddingLeft = l;
        node->paddingTop = t;
        node->paddingRight = r;
        node->paddingBottom = b;
    }
    return 0;
}

int ScriptUiLayoutApi::luaSetMargin(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    float l = static_cast<float>(luaL_checknumber(L, 2));
    float t = static_cast<float>(luaL_checknumber(L, 3));
    float r = static_cast<float>(luaL_checknumber(L, 4));
    float b = static_cast<float>(luaL_checknumber(L, 5));
    if (UINode* node = selfFromUpvalue(L)->tree_.findNode(id)) {
        node->marginLeft = l;
        node->marginTop = t;
        node->marginRight = r;
        node->marginBottom = b;
    }
    return 0;
}

int ScriptUiLayoutApi::luaSetColor(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    float r = static_cast<float>(luaL_checknumber(L, 2));
    float g = static_cast<float>(luaL_checknumber(L, 3));
    float b = static_cast<float>(luaL_checknumber(L, 4));
    float a = static_cast<float>(luaL_optnumber(L, 5, 1.0));
    if (UINode* node = selfFromUpvalue(L)->tree_.findNode(id)) node->color = glm::vec4(r, g, b, a);
    return 0;
}

int ScriptUiLayoutApi::luaSetColorEnd(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    float r = static_cast<float>(luaL_checknumber(L, 2));
    float g = static_cast<float>(luaL_checknumber(L, 3));
    float b = static_cast<float>(luaL_checknumber(L, 4));
    float a = static_cast<float>(luaL_optnumber(L, 5, 1.0));
    if (UINode* node = selfFromUpvalue(L)->tree_.findNode(id)) node->colorEnd = glm::vec4(r, g, b, a);
    return 0;
}

int ScriptUiLayoutApi::luaSetFillFromValue(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    bool enabled = lua_toboolean(L, 2) != 0;
    if (UINode* node = selfFromUpvalue(L)->tree_.findNode(id)) node->fillFromValue = enabled;
    return 0;
}

int ScriptUiLayoutApi::luaSetText(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    const char* text = luaL_checkstring(L, 2);
    if (UINode* node = selfFromUpvalue(L)->tree_.findNode(id)) node->text = text;
    return 0;
}

int ScriptUiLayoutApi::luaSetTextScale(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    float scale = static_cast<float>(luaL_checknumber(L, 2));
    if (UINode* node = selfFromUpvalue(L)->tree_.findNode(id)) node->textScale = scale;
    return 0;
}

// Kronos: `refHolder` ties the Luau function's real lua_ref() lifetime
// to this binding closure's own lifetime -- when the bound UINode is
// removed (UILayoutTree::removeNode()), the std::function captured on
// it is destroyed, refHolder's refcount drops to zero, and its deleter
// real-unrefs the Luau registry slot. Without this, every bindValue/
// bindText/bindColor call would leak one registry entry for the rest of
// the VM's lifetime -- the same real leak Scripting's own EventCallback
// list avoids by lua_unref()'ing on teardown, just scoped to node
// removal instead of VM shutdown since UI nodes churn far more often
// than loaded scripts do.
namespace {
std::shared_ptr<void> makeRefHolder(lua_State* owner, int ref) {
    return std::shared_ptr<void>(nullptr, [owner, ref](void*) { lua_unref(owner, ref); });
}
} // namespace

int ScriptUiLayoutApi::luaBindValue(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    luaL_checktype(L, 2, LUA_TFUNCTION);
    ScriptUiLayoutApi* self = selfFromUpvalue(L);
    UINode* node = self->tree_.findNode(id);
    if (!node) return 0;
    lua_State* owner = lua_mainthread(L);
    lua_pushvalue(L, 2);
    int ref = lua_ref(L, -1);
    lua_pop(L, 1);
    auto refHolder = makeRefHolder(owner, ref);
    node->bindValue = [self, owner, ref, refHolder]() -> float { return self->callNumber(owner, ref, 0.0f); };
    return 0;
}

int ScriptUiLayoutApi::luaBindText(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    luaL_checktype(L, 2, LUA_TFUNCTION);
    ScriptUiLayoutApi* self = selfFromUpvalue(L);
    UINode* node = self->tree_.findNode(id);
    if (!node) return 0;
    lua_State* owner = lua_mainthread(L);
    lua_pushvalue(L, 2);
    int ref = lua_ref(L, -1);
    lua_pop(L, 1);
    auto refHolder = makeRefHolder(owner, ref);
    node->bindText = [self, owner, ref, refHolder]() -> std::string { return self->callString(owner, ref, ""); };
    return 0;
}

int ScriptUiLayoutApi::luaBindColor(lua_State* L) {
    int id = static_cast<int>(luaL_checkinteger(L, 1));
    luaL_checktype(L, 2, LUA_TFUNCTION);
    ScriptUiLayoutApi* self = selfFromUpvalue(L);
    UINode* node = self->tree_.findNode(id);
    if (!node) return 0;
    lua_State* owner = lua_mainthread(L);
    lua_pushvalue(L, 2);
    int ref = lua_ref(L, -1);
    lua_pop(L, 1);
    auto refHolder = makeRefHolder(owner, ref);
    glm::vec4 fallback = node->color;
    node->bindColor = [self, owner, ref, refHolder, fallback]() -> glm::vec4 { return self->callColor(owner, ref, fallback); };
    return 0;
}

void ScriptUiLayoutApi::renderInto(UIRenderer& uiRenderer, glm::vec2 availableSize, glm::vec2 screenOffset) {
    tree_.resolveBindings();
    tree_.computeLayout(availableSize, uiRenderer.textMeasureFn());
    uiRenderer.drawLayoutTree(tree_, tree_.root(), screenOffset);
}

void ScriptUiLayoutApi::registerInto(lua_State* L) {
    struct Entry {
        const char* name;
        lua_CFunction fn;
    };
    static constexpr Entry kEntries[] = {
        {"node", &ScriptUiLayoutApi::luaNode},
        {"remove", &ScriptUiLayoutApi::luaRemove},
        {"setParent", &ScriptUiLayoutApi::luaSetParent},
        {"setRoot", &ScriptUiLayoutApi::luaSetRoot},
        {"setDirection", &ScriptUiLayoutApi::luaSetDirection},
        {"setJustify", &ScriptUiLayoutApi::luaSetJustify},
        {"setAlign", &ScriptUiLayoutApi::luaSetAlign},
        {"setSize", &ScriptUiLayoutApi::luaSetSize},
        {"setFlex", &ScriptUiLayoutApi::luaSetFlex},
        {"setGap", &ScriptUiLayoutApi::luaSetGap},
        {"setPadding", &ScriptUiLayoutApi::luaSetPadding},
        {"setMargin", &ScriptUiLayoutApi::luaSetMargin},
        {"setColor", &ScriptUiLayoutApi::luaSetColor},
        {"setColorEnd", &ScriptUiLayoutApi::luaSetColorEnd},
        {"setFillFromValue", &ScriptUiLayoutApi::luaSetFillFromValue},
        {"setText", &ScriptUiLayoutApi::luaSetText},
        {"setTextScale", &ScriptUiLayoutApi::luaSetTextScale},
        {"bindValue", &ScriptUiLayoutApi::luaBindValue},
        {"bindText", &ScriptUiLayoutApi::luaBindText},
        {"bindColor", &ScriptUiLayoutApi::luaBindColor},
    };

    lua_newtable(L);
    for (const Entry& entry : kEntries) {
        lua_pushlightuserdata(L, this);
        lua_pushcclosure(L, entry.fn, entry.name, 1);
        lua_setfield(L, -2, entry.name);
    }
    lua_setglobal(L, "uiLayout");
}

} // namespace engine::core
