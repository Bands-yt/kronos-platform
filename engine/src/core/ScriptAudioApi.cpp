#include "core/ScriptAudioApi.hpp"

#include <algorithm>
#include <functional>

#include <lua.h>
#include <lualib.h>

#include "core/Audio.hpp"
#include "core/Components.hpp"
#include "core/ECS.hpp"

namespace engine::core {

namespace {

ScriptAudioApi* self(lua_State* L) { return static_cast<ScriptAudioApi*>(lua_tolightuserdata(L, lua_upvalueindex(1))); }

EntityId idFromLua(lua_State* L, int idx) {
    return static_cast<EntityId>(static_cast<uint32_t>(luaL_checknumber(L, idx)));
}

} // namespace

ScriptAudioApi::ScriptAudioApi(Audio& audio, ECS& ecs) : audio_(audio), ecs_(ecs) {}

namespace {

AudioSource* sourceFor(lua_State* L, ECS& ecs) {
    const EntityId entity = idFromLua(L, 1);
    if (!ecs.raw().valid(entity)) return nullptr;
    return ecs.tryGetComponent<AudioSource>(entity);
}

bool updateBus(Audio& audio, const char* name, const std::function<void(MixerBus&)>& change) {
    MixerConfig config = audio.mixer().config();
    MixerBus* bus = config.bus(name);
    if (!bus) return false;
    change(*bus);
    return audio.mixer().setConfig(config);
}

} // namespace

int ScriptAudioApi::luaSnapshot(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    const auto intensity = static_cast<float>(luaL_optnumber(L, 2, 1.0));
    const auto fade = static_cast<float>(luaL_optnumber(L, 3, 0.0));
    lua_pushboolean(L, self(L)->audio_.mixer().setSnapshot(name, intensity, fade));
    return 1;
}

int ScriptAudioApi::luaSnapshotIntensity(lua_State* L) {
    lua_pushnumber(L, self(L)->audio_.mixer().snapshotIntensity(luaL_checkstring(L, 1)));
    return 1;
}

int ScriptAudioApi::luaSetBusVolume(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    const auto db = static_cast<float>(luaL_checknumber(L, 2));
    lua_pushboolean(L, updateBus(self(L)->audio_, name, [&](MixerBus& bus) { bus.volumeDb = std::clamp(db, -80.0f, 24.0f); }));
    return 1;
}

int ScriptAudioApi::luaBusVolume(lua_State* L) {
    lua_pushnumber(L, self(L)->audio_.mixer().effectiveValue(luaL_checkstring(L, 1), MixerParam::Volume));
    return 1;
}

int ScriptAudioApi::luaSetBusMuted(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    const bool muted = lua_toboolean(L, 2) != 0;
    lua_pushboolean(L, updateBus(self(L)->audio_, name, [&](MixerBus& bus) { bus.muted = muted; }));
    return 1;
}

int ScriptAudioApi::luaBusLevel(lua_State* L) {
    lua_pushnumber(L, self(L)->audio_.mixer().meter(luaL_checkstring(L, 1)).rmsDb);
    return 1;
}

int ScriptAudioApi::luaPlay(lua_State* L) {
    AudioSource* source = sourceFor(L, self(L)->ecs_);
    if (!source) return 0;
    self(L)->audio_.playFromOffset(source->soundHandle, 0.0);
    source->playing = true;
    lua_pushboolean(L, 1);
    return 1;
}

int ScriptAudioApi::luaStop(lua_State* L) {
    if (AudioSource* source = sourceFor(L, self(L)->ecs_)) source->playing = false;
    return 0;
}

int ScriptAudioApi::luaIsPlaying(lua_State* L) {
    AudioSource* source = sourceFor(L, self(L)->ecs_);
    lua_pushboolean(L, source && source->playing && self(L)->audio_.isSoundPlaying(source->soundHandle));
    return 1;
}

int ScriptAudioApi::luaSetVolume(lua_State* L) {
    if (AudioSource* source = sourceFor(L, self(L)->ecs_)) {
        source->volume = std::clamp(static_cast<float>(luaL_checknumber(L, 2)), 0.0f, 4.0f);
    }
    return 0;
}

int ScriptAudioApi::luaSetPitch(lua_State* L) {
    if (AudioSource* source = sourceFor(L, self(L)->ecs_)) {
        source->pitch = std::clamp(static_cast<float>(luaL_checknumber(L, 2)), 0.05f, 8.0f);
    }
    return 0;
}

int ScriptAudioApi::luaSetBus(lua_State* L) {
    if (AudioSource* source = sourceFor(L, self(L)->ecs_)) source->bus = luaL_checkstring(L, 2);
    return 0;
}

void ScriptAudioApi::registerInto(lua_State* L) {
    struct Entry {
        const char* name;
        lua_CFunction fn;
    };
    static constexpr Entry kEntries[] = {
        {"snapshot", &ScriptAudioApi::luaSnapshot},
        {"snapshotIntensity", &ScriptAudioApi::luaSnapshotIntensity},
        {"setBusVolume", &ScriptAudioApi::luaSetBusVolume},
        {"busVolume", &ScriptAudioApi::luaBusVolume},
        {"setBusMuted", &ScriptAudioApi::luaSetBusMuted},
        {"busLevel", &ScriptAudioApi::luaBusLevel},
        {"play", &ScriptAudioApi::luaPlay},
        {"stop", &ScriptAudioApi::luaStop},
        {"isPlaying", &ScriptAudioApi::luaIsPlaying},
        {"setVolume", &ScriptAudioApi::luaSetVolume},
        {"setPitch", &ScriptAudioApi::luaSetPitch},
        {"setBus", &ScriptAudioApi::luaSetBus},
    };
    lua_newtable(L);
    for (const Entry& entry : kEntries) {
        lua_pushlightuserdata(L, this);
        lua_pushcclosure(L, entry.fn, entry.name, 1);
        lua_setfield(L, -2, entry.name);
    }
    lua_setglobal(L, "audio");
}

} // namespace engine::core
