#include "studio/ScriptRenderApi.hpp"

#include <cstring>

#include <lua.h>
#include <lualib.h>

#include "core/Renderer.hpp"

namespace engine::studio {

namespace {
// Every luaX function reads `this` off upvalue 1 -- same closure-per-
// function pattern core::ScriptWorldApi.cpp's own selfFromUpvalue()
// establishes.
ScriptRenderApi* selfFromUpvalue(lua_State* L) {
    return static_cast<ScriptRenderApi*>(lua_tolightuserdata(L, lua_upvalueindex(1)));
}
} // namespace

ScriptRenderApi::ScriptRenderApi(core::Renderer& renderer) : renderer_(renderer) {}

int ScriptRenderApi::luaSetExposure(lua_State* L) {
    float exposure = static_cast<float>(luaL_checknumber(L, 1));
    selfFromUpvalue(L)->renderer_.setExposure(exposure);
    return 0;
}

int ScriptRenderApi::luaExposure(lua_State* L) {
    lua_pushnumber(L, selfFromUpvalue(L)->renderer_.exposure());
    return 1;
}

int ScriptRenderApi::luaSetBloomSettings(lua_State* L) {
    float threshold = static_cast<float>(luaL_checknumber(L, 1));
    float softKnee = static_cast<float>(luaL_checknumber(L, 2));
    float intensity = static_cast<float>(luaL_checknumber(L, 3));
    selfFromUpvalue(L)->renderer_.setBloomSettings(threshold, softKnee, intensity);
    return 0;
}

int ScriptRenderApi::luaBloomSettings(lua_State* L) {
    core::Renderer& renderer = selfFromUpvalue(L)->renderer_;
    lua_pushnumber(L, renderer.bloomThreshold());
    lua_pushnumber(L, renderer.bloomSoftKnee());
    lua_pushnumber(L, renderer.bloomIntensity());
    return 3;
}

int ScriptRenderApi::luaSetCinematicMode(lua_State* L) {
    luaL_checktype(L, 1, LUA_TBOOLEAN);
    selfFromUpvalue(L)->renderer_.setCinematicMode(lua_toboolean(L, 1) != 0);
    return 0;
}

int ScriptRenderApi::luaIsCinematicModeEnabled(lua_State* L) {
    lua_pushboolean(L, selfFromUpvalue(L)->renderer_.isCinematicModeEnabled() ? 1 : 0);
    return 1;
}

int ScriptRenderApi::luaSetDepthOfFieldEnabled(lua_State* L) {
    luaL_checktype(L, 1, LUA_TBOOLEAN);
    selfFromUpvalue(L)->renderer_.setDepthOfFieldEnabled(lua_toboolean(L, 1) != 0);
    return 0;
}

int ScriptRenderApi::luaIsDepthOfFieldEnabled(lua_State* L) {
    lua_pushboolean(L, selfFromUpvalue(L)->renderer_.isDepthOfFieldEnabled() ? 1 : 0);
    return 1;
}

int ScriptRenderApi::luaSetDepthOfFieldParams(lua_State* L) {
    float focusDistance = static_cast<float>(luaL_checknumber(L, 1));
    float focusRange = static_cast<float>(luaL_checknumber(L, 2));
    float maxCoCRadiusPx = static_cast<float>(luaL_checknumber(L, 3));
    selfFromUpvalue(L)->renderer_.setDepthOfFieldParams(focusDistance, focusRange, maxCoCRadiusPx);
    return 0;
}

int ScriptRenderApi::luaDepthOfFieldParams(lua_State* L) {
    core::Renderer& renderer = selfFromUpvalue(L)->renderer_;
    lua_pushnumber(L, renderer.dofFocusDistance());
    lua_pushnumber(L, renderer.dofFocusRange());
    lua_pushnumber(L, renderer.dofMaxCoCRadiusPx());
    return 3;
}

int ScriptRenderApi::luaSetTonemapOperator(lua_State* L) {
    const char* op = luaL_checkstring(L, 1);
    if (std::strcmp(op, "aces") == 0) {
        selfFromUpvalue(L)->renderer_.setTonemapOperator(core::Renderer::TonemapOperator::AcesFilm);
    } else if (std::strcmp(op, "agx") == 0) {
        selfFromUpvalue(L)->renderer_.setTonemapOperator(core::Renderer::TonemapOperator::AgX);
    } else {
        // luaL_error() long-jumps out of this C function -- nothing
        // below it runs, matching core::ScriptSecurity's own documented
        // convention for this exact call.
        luaL_error(L, "render.setTonemapOperator: unknown operator \"%s\" (expected \"aces\" or \"agx\")", op);
    }
    return 0;
}

int ScriptRenderApi::luaTonemapOperator(lua_State* L) {
    core::Renderer::TonemapOperator op = selfFromUpvalue(L)->renderer_.tonemapOperator();
    lua_pushstring(L, op == core::Renderer::TonemapOperator::AgX ? "agx" : "aces");
    return 1;
}

int ScriptRenderApi::luaSetColorGradingLutStrength(lua_State* L) {
    float strength = static_cast<float>(luaL_checknumber(L, 1));
    selfFromUpvalue(L)->renderer_.setColorGradingLutStrength(strength);
    return 0;
}

int ScriptRenderApi::luaColorGradingLutStrength(lua_State* L) {
    lua_pushnumber(L, selfFromUpvalue(L)->renderer_.colorGradingLutStrength());
    return 1;
}

int ScriptRenderApi::luaLoadColorGradingLut(lua_State* L) {
    const char* path = luaL_checkstring(L, 1);
    std::string error;
    bool ok = selfFromUpvalue(L)->renderer_.loadColorGradingLut(path, error);
    lua_pushboolean(L, ok ? 1 : 0);
    if (ok) return 1;
    lua_pushstring(L, error.c_str());
    return 2;
}

int ScriptRenderApi::luaResetColorGradingLutToIdentity(lua_State* L) {
    std::string error;
    bool ok = selfFromUpvalue(L)->renderer_.resetColorGradingLutToIdentity(error);
    lua_pushboolean(L, ok ? 1 : 0);
    if (ok) return 1;
    lua_pushstring(L, error.c_str());
    return 2;
}

void ScriptRenderApi::registerInto(lua_State* L) {
    struct Entry {
        const char* name;
        lua_CFunction fn;
    };
    static constexpr Entry kEntries[] = {
        {"setExposure", &ScriptRenderApi::luaSetExposure},
        {"exposure", &ScriptRenderApi::luaExposure},
        {"setBloomSettings", &ScriptRenderApi::luaSetBloomSettings},
        {"bloomSettings", &ScriptRenderApi::luaBloomSettings},
        {"setCinematicMode", &ScriptRenderApi::luaSetCinematicMode},
        {"isCinematicModeEnabled", &ScriptRenderApi::luaIsCinematicModeEnabled},
        {"setDepthOfFieldEnabled", &ScriptRenderApi::luaSetDepthOfFieldEnabled},
        {"isDepthOfFieldEnabled", &ScriptRenderApi::luaIsDepthOfFieldEnabled},
        {"setDepthOfFieldParams", &ScriptRenderApi::luaSetDepthOfFieldParams},
        {"depthOfFieldParams", &ScriptRenderApi::luaDepthOfFieldParams},
        {"setTonemapOperator", &ScriptRenderApi::luaSetTonemapOperator},
        {"tonemapOperator", &ScriptRenderApi::luaTonemapOperator},
        {"setColorGradingLutStrength", &ScriptRenderApi::luaSetColorGradingLutStrength},
        {"colorGradingLutStrength", &ScriptRenderApi::luaColorGradingLutStrength},
        {"loadColorGradingLut", &ScriptRenderApi::luaLoadColorGradingLut},
        {"resetColorGradingLutToIdentity", &ScriptRenderApi::luaResetColorGradingLutToIdentity},
    };

    lua_newtable(L);
    for (const Entry& entry : kEntries) {
        lua_pushlightuserdata(L, this);
        lua_pushcclosure(L, entry.fn, entry.name, 1);
        lua_setfield(L, -2, entry.name);
    }
    lua_setglobal(L, "render");
}

} // namespace engine::studio
