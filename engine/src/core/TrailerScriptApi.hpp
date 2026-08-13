#pragma once

struct lua_State;

namespace engine::trailer {
class TrailerDirector;
}

namespace engine::core {

// Sprint 15 ("TNT-Wars Trailer Production")'s real, deliberately small
// Luau binding surface for `TrailerScript.lua` -- a global `cinematic`
// table, flat function calls exactly matching `world`'s own real
// convention (core::ScriptWorldApi.hpp's own header comment on why: no
// Roblox-shaped Instance-metatable illusion, plain numbers/strings in,
// plain numbers/strings out).
//
// Deliberately thin: every function here either starts/stops/queries the
// real trailer::TrailerDirector's own already-deterministic playback, or
// adjusts a real, already-exposed live knob (playback speed, camera
// shake) -- it does NOT re-expose per-beat camera-path/trigger authoring
// to Lua. The trailer's real scene order/camera paths/class actions/
// ultimate triggers/map transitions are real, tested, deterministic C++
// content (trailer::TrailerScenes::buildTntWarsTrailerBeats()) that
// `cinematic.startCapture()` sets in motion -- see this sprint's own
// README section for the full, honest reasoning (in short: the brief's
// own "deterministic, reproducible" requirement is easiest to keep
// exactly true when the beat content itself is fixed, tested C++ data,
// not re-derived ad hoc by a loosely-typed script each run).
// `cinematic.jumpToBeat()` still gives a script (or the Studio Trailer
// Panel) real, immediate control over *which* beat plays when, without
// needing to re-author what that beat does.
class TrailerScriptApi {
public:
    explicit TrailerScriptApi(trailer::TrailerDirector& director);

    void registerInto(lua_State* L);

private:
    static int luaStartCapture(lua_State* L);
    static int luaStopCapture(lua_State* L);
    static int luaIsCapturing(lua_State* L);
    static int luaIsFinished(lua_State* L);
    static int luaCurrentBeatName(lua_State* L);
    static int luaElapsedSeconds(lua_State* L);
    static int luaFramesSaved(lua_State* L);
    static int luaTotalDurationSeconds(lua_State* L);
    static int luaSetPlaybackSpeed(lua_State* L);
    static int luaSetCameraShake(lua_State* L);
    static int luaSetCameraShakeProfile(lua_State* L);
    static int luaBeatCount(lua_State* L);
    static int luaBeatName(lua_State* L);
    static int luaJumpToBeat(lua_State* L);
    static int luaDefaultOutputDirectory(lua_State* L);

    trailer::TrailerDirector& director_;
};

} // namespace engine::core
