#include "core/TrailerScriptApi.hpp"

#include <cstring>

#include <lua.h>
#include <lualib.h>

#include "trailer/TrailerDirector.hpp"
#include "trailer/TrailerTimeline.hpp"

namespace engine::core {

namespace {
TrailerScriptApi* selfFromUpvalue(lua_State* L) {
    return static_cast<TrailerScriptApi*>(lua_tolightuserdata(L, lua_upvalueindex(1)));
}
} // namespace

TrailerScriptApi::TrailerScriptApi(trailer::TrailerDirector& director) : director_(director) {}

int TrailerScriptApi::luaStartCapture(lua_State* L) {
    const char* outputDir = luaL_checkstring(L, 1);
    float fps = static_cast<float>(luaL_optnumber(L, 2, 24.0));
    selfFromUpvalue(L)->director_.startCapture(outputDir, fps);
    return 0;
}

int TrailerScriptApi::luaStopCapture(lua_State* L) {
    selfFromUpvalue(L)->director_.stopCapture();
    return 0;
}

int TrailerScriptApi::luaIsCapturing(lua_State* L) {
    lua_pushboolean(L, selfFromUpvalue(L)->director_.isCapturing() ? 1 : 0);
    return 1;
}

int TrailerScriptApi::luaIsFinished(lua_State* L) {
    lua_pushboolean(L, selfFromUpvalue(L)->director_.isFinished() ? 1 : 0);
    return 1;
}

int TrailerScriptApi::luaCurrentBeatName(lua_State* L) {
    trailer::TrailerDirector& director = selfFromUpvalue(L)->director_;
    trailer::TrailerTimeline::SceneQuery query = director.timeline().queryAt(director.elapsedSeconds());
    if (query.sceneIndex < 0) {
        lua_pushstring(L, "(finished)");
    } else {
        lua_pushstring(L, director.beats()[static_cast<size_t>(query.sceneIndex)].name.c_str());
    }
    return 1;
}

int TrailerScriptApi::luaElapsedSeconds(lua_State* L) {
    lua_pushnumber(L, static_cast<double>(selfFromUpvalue(L)->director_.elapsedSeconds()));
    return 1;
}

int TrailerScriptApi::luaFramesSaved(lua_State* L) {
    lua_pushnumber(L, static_cast<double>(selfFromUpvalue(L)->director_.framesSaved()));
    return 1;
}

int TrailerScriptApi::luaTotalDurationSeconds(lua_State* L) {
    lua_pushnumber(L, static_cast<double>(selfFromUpvalue(L)->director_.timeline().totalDurationSeconds()));
    return 1;
}

int TrailerScriptApi::luaSetPlaybackSpeed(lua_State* L) {
    float speed = static_cast<float>(luaL_checknumber(L, 1));
    selfFromUpvalue(L)->director_.setPlaybackSpeed(speed);
    return 0;
}

int TrailerScriptApi::luaSetCameraShake(lua_State* L) {
    float amplitude = static_cast<float>(luaL_checknumber(L, 1));
    float frequencyHz = static_cast<float>(luaL_checknumber(L, 2));
    selfFromUpvalue(L)->director_.setCameraShake(amplitude, frequencyHz);
    return 0;
}

// Sprint 16 ("Cinematic Graphics" Phase 4) -- real, named shake presets
// over the same real director_.setCameraShake() mechanism, see
// TrailerDirector::ShakeProfile's own header comment for the real
// vocabulary. Unrecognized names real-fall-back to None (a safe, silent
// "shake off" rather than a Lua error over a typo'd string).
int TrailerScriptApi::luaSetCameraShakeProfile(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    trailer::TrailerDirector::ShakeProfile profile = trailer::TrailerDirector::ShakeProfile::None;
    if (std::strcmp(name, "Subtle") == 0) profile = trailer::TrailerDirector::ShakeProfile::Subtle;
    else if (std::strcmp(name, "Handheld") == 0) profile = trailer::TrailerDirector::ShakeProfile::Handheld;
    else if (std::strcmp(name, "Impact") == 0) profile = trailer::TrailerDirector::ShakeProfile::Impact;
    else if (std::strcmp(name, "Intense") == 0) profile = trailer::TrailerDirector::ShakeProfile::Intense;
    selfFromUpvalue(L)->director_.setCameraShakeProfile(profile);
    return 0;
}

int TrailerScriptApi::luaBeatCount(lua_State* L) {
    lua_pushnumber(L, static_cast<double>(selfFromUpvalue(L)->director_.beats().size()));
    return 1;
}

int TrailerScriptApi::luaBeatName(lua_State* L) {
    // Real, 0-based index -- matching this engine's own established
    // `world.*` convention (plain integers, no Lua 1-based-array
    // reindexing games), see this class's own header comment.
    int index = static_cast<int>(luaL_checknumber(L, 1));
    const trailer::TrailerDirector& director = selfFromUpvalue(L)->director_;
    if (index < 0 || index >= static_cast<int>(director.beats().size())) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushstring(L, director.beats()[static_cast<size_t>(index)].name.c_str());
    return 1;
}

int TrailerScriptApi::luaJumpToBeat(lua_State* L) {
    int index = static_cast<int>(luaL_checknumber(L, 1));
    selfFromUpvalue(L)->director_.seekToBeat(index);
    return 0;
}

int TrailerScriptApi::luaDefaultOutputDirectory(lua_State* L) {
    lua_pushstring(L, selfFromUpvalue(L)->director_.defaultOutputDirectory().c_str());
    return 1;
}

void TrailerScriptApi::registerInto(lua_State* L) {
    struct Entry {
        const char* name;
        lua_CFunction fn;
    };
    static constexpr Entry kEntries[] = {
        {"startCapture", &TrailerScriptApi::luaStartCapture},
        {"stopCapture", &TrailerScriptApi::luaStopCapture},
        {"isCapturing", &TrailerScriptApi::luaIsCapturing},
        {"isFinished", &TrailerScriptApi::luaIsFinished},
        {"currentBeatName", &TrailerScriptApi::luaCurrentBeatName},
        {"elapsedSeconds", &TrailerScriptApi::luaElapsedSeconds},
        {"framesSaved", &TrailerScriptApi::luaFramesSaved},
        {"totalDurationSeconds", &TrailerScriptApi::luaTotalDurationSeconds},
        {"setPlaybackSpeed", &TrailerScriptApi::luaSetPlaybackSpeed},
        {"setCameraShake", &TrailerScriptApi::luaSetCameraShake},
        {"setCameraShakeProfile", &TrailerScriptApi::luaSetCameraShakeProfile},
        {"beatCount", &TrailerScriptApi::luaBeatCount},
        {"beatName", &TrailerScriptApi::luaBeatName},
        {"jumpToBeat", &TrailerScriptApi::luaJumpToBeat},
        {"defaultOutputDirectory", &TrailerScriptApi::luaDefaultOutputDirectory},
    };

    lua_newtable(L);
    for (const Entry& entry : kEntries) {
        lua_pushlightuserdata(L, this);
        lua_pushcclosure(L, entry.fn, entry.name, 1);
        lua_setfield(L, -2, entry.name);
    }
    lua_setglobal(L, "cinematic");
}

} // namespace engine::core
