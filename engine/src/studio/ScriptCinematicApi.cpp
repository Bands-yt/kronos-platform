#include "studio/ScriptCinematicApi.hpp"

#include <lua.h>
#include <lualib.h>

namespace engine::studio {

namespace {
ScriptCinematicApi* selfFromUpvalue(lua_State* L) {
    return static_cast<ScriptCinematicApi*>(lua_tolightuserdata(L, lua_upvalueindex(1)));
}

// Bounds-checked track lookup shared by every function below that
// addresses a track by index -- a real, honest nil/false/no-op for a
// stale or out-of-range index rather than a crash, same contract every
// other index-taking function in this codebase's Luau bindings already
// carries (see core::EditableMesh's own out-of-range face/edge handling).
cinematic::SequencerTrack* trackAt(cinematic::Sequence& sequence, int index) {
    auto& tracks = sequence.mutableTracks();
    if (index < 0 || static_cast<size_t>(index) >= tracks.size()) return nullptr;
    return &tracks[static_cast<size_t>(index)];
}
} // namespace

ScriptCinematicApi::ScriptCinematicApi(cinematic::Sequence& sequence, cinematic::CameraRail& rail,
                                        cinematic::ExportSettings& exportSettings)
    : sequence_(sequence), rail_(rail), exportSettings_(exportSettings) {}

int ScriptCinematicApi::luaAddTrack(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    auto kind = static_cast<cinematic::TrackKind>(static_cast<uint8_t>(luaL_checknumber(L, 2)));
    ScriptCinematicApi* self = selfFromUpvalue(L);
    size_t indexBefore = self->sequence_.tracks().size();
    (void)self->sequence_.addTrack(name, kind);
    lua_pushnumber(L, static_cast<double>(indexBefore));
    return 1;
}

int ScriptCinematicApi::luaTrackCount(lua_State* L) {
    lua_pushnumber(L, static_cast<double>(selfFromUpvalue(L)->sequence_.tracks().size()));
    return 1;
}

int ScriptCinematicApi::luaRemoveTrack(lua_State* L) {
    int index = static_cast<int>(luaL_checknumber(L, 1));
    ScriptCinematicApi* self = selfFromUpvalue(L);
    bool ok = index >= 0 && static_cast<size_t>(index) < self->sequence_.tracks().size();
    if (ok) self->sequence_.removeTrack(static_cast<size_t>(index));
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}

int ScriptCinematicApi::luaSetTrackTarget(lua_State* L) {
    int index = static_cast<int>(luaL_checknumber(L, 1));
    auto targetId = static_cast<uint64_t>(luaL_checknumber(L, 2));
    auto* track = trackAt(selfFromUpvalue(L)->sequence_, index);
    if (track != nullptr) track->targetId = targetId;
    lua_pushboolean(L, track != nullptr ? 1 : 0);
    return 1;
}

int ScriptCinematicApi::luaSetTrackMuted(lua_State* L) {
    int index = static_cast<int>(luaL_checknumber(L, 1));
    bool muted = lua_toboolean(L, 2) != 0;
    auto* track = trackAt(selfFromUpvalue(L)->sequence_, index);
    if (track != nullptr) track->muted = muted;
    lua_pushboolean(L, track != nullptr ? 1 : 0);
    return 1;
}

int ScriptCinematicApi::luaAddKeyframe(lua_State* L) {
    int index = static_cast<int>(luaL_checknumber(L, 1));
    const char* channelName = luaL_checkstring(L, 2);
    float timeSeconds = static_cast<float>(luaL_checknumber(L, 3));
    float value = static_cast<float>(luaL_checknumber(L, 4));
    auto mode = static_cast<cinematic::InterpolationMode>(
        static_cast<uint8_t>(lua_isnone(L, 5) ? static_cast<double>(cinematic::InterpolationMode::Cubic) : luaL_checknumber(L, 5)));
    auto* track = trackAt(selfFromUpvalue(L)->sequence_, index);
    if (track != nullptr) {
        cinematic::Keyframe key;
        key.timeSeconds = timeSeconds;
        key.value = value;
        key.mode = mode;
        cinematic::insertKeyframe(track->channel(channelName).keys, key);
    }
    lua_pushboolean(L, track != nullptr ? 1 : 0);
    return 1;
}

int ScriptCinematicApi::luaSampleChannel(lua_State* L) {
    const char* trackName = luaL_checkstring(L, 1);
    const char* channelName = luaL_checkstring(L, 2);
    float timeSeconds = static_cast<float>(luaL_checknumber(L, 3));
    float fallback = lua_isnone(L, 4) ? 0.0f : static_cast<float>(luaL_checknumber(L, 4));
    ScriptCinematicApi* self = selfFromUpvalue(L);
    lua_pushnumber(L, self->sequence_.sampleChannel(trackName, channelName, timeSeconds, fallback));
    return 1;
}

int ScriptCinematicApi::luaAddEvent(lua_State* L) {
    int index = static_cast<int>(luaL_checknumber(L, 1));
    float timeSeconds = static_cast<float>(luaL_checknumber(L, 2));
    const char* payload = luaL_checkstring(L, 3);
    auto* track = trackAt(selfFromUpvalue(L)->sequence_, index);
    if (track != nullptr) track->events.push_back(cinematic::TrackEvent{timeSeconds, payload});
    lua_pushboolean(L, track != nullptr ? 1 : 0);
    return 1;
}

int ScriptCinematicApi::luaSetFrameRate(lua_State* L) {
    auto rate = static_cast<cinematic::SequenceFrameRate>(static_cast<uint8_t>(luaL_checknumber(L, 1)));
    selfFromUpvalue(L)->sequence_.setFrameRate(rate);
    return 0;
}

int ScriptCinematicApi::luaFrameRate(lua_State* L) {
    lua_pushnumber(L, static_cast<double>(static_cast<uint8_t>(selfFromUpvalue(L)->sequence_.frameRate())));
    return 1;
}

int ScriptCinematicApi::luaDurationSeconds(lua_State* L) {
    lua_pushnumber(L, selfFromUpvalue(L)->sequence_.durationSeconds());
    return 1;
}

int ScriptCinematicApi::luaPlay(lua_State* L) {
    selfFromUpvalue(L)->sequence_.play();
    return 0;
}

int ScriptCinematicApi::luaPause(lua_State* L) {
    selfFromUpvalue(L)->sequence_.pause();
    return 0;
}

int ScriptCinematicApi::luaIsPlaying(lua_State* L) {
    lua_pushboolean(L, selfFromUpvalue(L)->sequence_.isPlaying() ? 1 : 0);
    return 1;
}

int ScriptCinematicApi::luaSetPlayhead(lua_State* L) {
    selfFromUpvalue(L)->sequence_.setPlayhead(static_cast<float>(luaL_checknumber(L, 1)));
    return 0;
}

int ScriptCinematicApi::luaPlayheadSeconds(lua_State* L) {
    lua_pushnumber(L, selfFromUpvalue(L)->sequence_.playheadSeconds());
    return 1;
}

int ScriptCinematicApi::luaStepFrames(lua_State* L) {
    selfFromUpvalue(L)->sequence_.stepFrames(static_cast<int>(luaL_checknumber(L, 1)));
    return 0;
}

int ScriptCinematicApi::luaSetLoopRegion(lua_State* L) {
    float start = static_cast<float>(luaL_checknumber(L, 1));
    float end = static_cast<float>(luaL_checknumber(L, 2));
    selfFromUpvalue(L)->sequence_.setLoopRegion(start, end);
    return 0;
}

int ScriptCinematicApi::luaLoopEnabled(lua_State* L) {
    lua_pushboolean(L, selfFromUpvalue(L)->sequence_.loopEnabled() ? 1 : 0);
    return 1;
}

int ScriptCinematicApi::luaAdvance(lua_State* L) {
    float deltaSeconds = static_cast<float>(luaL_checknumber(L, 1));
    std::vector<cinematic::TrackEvent> fired;
    selfFromUpvalue(L)->sequence_.advance(deltaSeconds, fired);
    lua_pushnumber(L, static_cast<double>(fired.size()));
    return 1;
}

int ScriptCinematicApi::luaAddRailPoint(lua_State* L) {
    cinematic::RailPoint point;
    point.position = glm::vec3(static_cast<float>(luaL_checknumber(L, 1)), static_cast<float>(luaL_checknumber(L, 2)),
                                static_cast<float>(luaL_checknumber(L, 3)));
    if (!lua_isnone(L, 4)) point.focalLengthMm = static_cast<float>(luaL_checknumber(L, 4));
    if (!lua_isnone(L, 5)) point.aperture = static_cast<float>(luaL_checknumber(L, 5));
    selfFromUpvalue(L)->rail_.addPoint(point);
    return 0;
}

int ScriptCinematicApi::luaRailPointCount(lua_State* L) {
    lua_pushnumber(L, static_cast<double>(selfFromUpvalue(L)->rail_.pointCount()));
    return 1;
}

int ScriptCinematicApi::luaClearRail(lua_State* L) {
    selfFromUpvalue(L)->rail_.clear();
    return 0;
}

int ScriptCinematicApi::luaSampleRailPosition(lua_State* L) {
    float t = static_cast<float>(luaL_checknumber(L, 1));
    glm::vec3 p = selfFromUpvalue(L)->rail_.samplePosition(t);
    lua_pushnumber(L, p.x);
    lua_pushnumber(L, p.y);
    lua_pushnumber(L, p.z);
    return 3;
}

int ScriptCinematicApi::luaSampleRail(lua_State* L) {
    float t = static_cast<float>(luaL_checknumber(L, 1));
    float deltaSeconds = lua_isnone(L, 2) ? 0.0f : static_cast<float>(luaL_checknumber(L, 2));
    cinematic::RailSample sample = selfFromUpvalue(L)->rail_.sample(t, deltaSeconds);
    lua_pushnumber(L, sample.position.x);
    lua_pushnumber(L, sample.position.y);
    lua_pushnumber(L, sample.position.z);
    lua_pushnumber(L, sample.camera.focalLengthMm);
    lua_pushnumber(L, sample.camera.aperture);
    lua_pushnumber(L, sample.camera.focusDistanceMeters);
    return 6;
}

int ScriptCinematicApi::luaSetPhysicalCamera(lua_State* L) {
    core::PhysicalCamera camera;
    camera.focalLengthMm = static_cast<float>(luaL_checknumber(L, 1));
    camera.aperture = static_cast<float>(luaL_checknumber(L, 2));
    camera.focusDistanceMeters = static_cast<float>(luaL_checknumber(L, 3));
    camera.sensor.widthMm = static_cast<float>(luaL_checknumber(L, 4));
    camera.sensor.heightMm = static_cast<float>(luaL_checknumber(L, 5));
    if (!lua_isnone(L, 6)) camera.isoSensitivity = static_cast<float>(luaL_checknumber(L, 6));
    if (!lua_isnone(L, 7)) camera.shutterSpeedSeconds = static_cast<float>(luaL_checknumber(L, 7));
    selfFromUpvalue(L)->physicalCamera_ = camera;
    return 0;
}

int ScriptCinematicApi::luaVerticalFovDegrees(lua_State* L) {
    lua_pushnumber(L, core::verticalFovDegrees(selfFromUpvalue(L)->physicalCamera_));
    return 1;
}

int ScriptCinematicApi::luaDepthOfFieldRangeMeters(lua_State* L) {
    float nearMeters = 0.0f, farMeters = 0.0f;
    bool hasFar = core::depthOfFieldRangeMeters(selfFromUpvalue(L)->physicalCamera_, nearMeters, farMeters);
    lua_pushnumber(L, nearMeters);
    lua_pushnumber(L, hasFar ? farMeters : -1.0); // -1 is the real, honest "far is at infinity" signal (see depthOfFieldRangeMeters()'s own doc comment)
    lua_pushboolean(L, hasFar ? 1 : 0);
    return 3;
}

int ScriptCinematicApi::luaHyperfocalDistanceMeters(lua_State* L) {
    lua_pushnumber(L, core::hyperfocalDistanceMeters(selfFromUpvalue(L)->physicalCamera_));
    return 1;
}

int ScriptCinematicApi::luaRelativeExposure(lua_State* L) {
    lua_pushnumber(L, core::relativeExposure(selfFromUpvalue(L)->physicalCamera_));
    return 1;
}

int ScriptCinematicApi::luaBuildExportSchedule(lua_State* L) {
    ScriptCinematicApi* self = selfFromUpvalue(L);
    std::string error;
    if (!cinematic::validateExportSettings(self->exportSettings_, error)) {
        self->lastSchedule_.clear();
        lua_pushnumber(L, 0.0);
        lua_pushstring(L, error.c_str());
        return 2;
    }
    cinematic::buildExportSchedule(self->exportSettings_, self->sequence_.durationSeconds(), self->lastSchedule_);
    lua_pushnumber(L, static_cast<double>(self->lastSchedule_.size()));
    lua_pushstring(L, "");
    return 2;
}

int ScriptCinematicApi::luaExportFrameCount(lua_State* L) {
    lua_pushnumber(L, static_cast<double>(selfFromUpvalue(L)->lastSchedule_.size()));
    return 1;
}

void ScriptCinematicApi::registerInto(lua_State* L) {
    struct Entry {
        const char* name;
        lua_CFunction fn;
    };
    static constexpr Entry kEntries[] = {
        {"addTrack", &ScriptCinematicApi::luaAddTrack},
        {"trackCount", &ScriptCinematicApi::luaTrackCount},
        {"removeTrack", &ScriptCinematicApi::luaRemoveTrack},
        {"setTrackTarget", &ScriptCinematicApi::luaSetTrackTarget},
        {"setTrackMuted", &ScriptCinematicApi::luaSetTrackMuted},
        {"addKeyframe", &ScriptCinematicApi::luaAddKeyframe},
        {"sampleChannel", &ScriptCinematicApi::luaSampleChannel},
        {"addEvent", &ScriptCinematicApi::luaAddEvent},
        {"setFrameRate", &ScriptCinematicApi::luaSetFrameRate},
        {"frameRate", &ScriptCinematicApi::luaFrameRate},
        {"durationSeconds", &ScriptCinematicApi::luaDurationSeconds},
        {"play", &ScriptCinematicApi::luaPlay},
        {"pause", &ScriptCinematicApi::luaPause},
        {"isPlaying", &ScriptCinematicApi::luaIsPlaying},
        {"setPlayhead", &ScriptCinematicApi::luaSetPlayhead},
        {"playheadSeconds", &ScriptCinematicApi::luaPlayheadSeconds},
        {"stepFrames", &ScriptCinematicApi::luaStepFrames},
        {"setLoopRegion", &ScriptCinematicApi::luaSetLoopRegion},
        {"loopEnabled", &ScriptCinematicApi::luaLoopEnabled},
        {"advance", &ScriptCinematicApi::luaAdvance},
        {"addRailPoint", &ScriptCinematicApi::luaAddRailPoint},
        {"railPointCount", &ScriptCinematicApi::luaRailPointCount},
        {"clearRail", &ScriptCinematicApi::luaClearRail},
        {"sampleRailPosition", &ScriptCinematicApi::luaSampleRailPosition},
        {"sampleRail", &ScriptCinematicApi::luaSampleRail},
        {"setPhysicalCamera", &ScriptCinematicApi::luaSetPhysicalCamera},
        {"verticalFovDegrees", &ScriptCinematicApi::luaVerticalFovDegrees},
        {"depthOfFieldRangeMeters", &ScriptCinematicApi::luaDepthOfFieldRangeMeters},
        {"hyperfocalDistanceMeters", &ScriptCinematicApi::luaHyperfocalDistanceMeters},
        {"relativeExposure", &ScriptCinematicApi::luaRelativeExposure},
        {"buildExportSchedule", &ScriptCinematicApi::luaBuildExportSchedule},
        {"exportFrameCount", &ScriptCinematicApi::luaExportFrameCount},
    };

    lua_newtable(L);
    for (const Entry& entry : kEntries) {
        lua_pushlightuserdata(L, this);
        lua_pushcclosure(L, entry.fn, entry.name, 1);
        lua_setfield(L, -2, entry.name);
    }
    lua_setglobal(L, "cinematic");
}

} // namespace engine::studio
