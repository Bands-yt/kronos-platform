#pragma once

struct lua_State;

namespace engine::core {

class Audio;
class ECS;

// The `audio` table for Luau scripts:
//   audio.snapshot(name, intensity = 1, fadeSeconds = 0) -> bool
//   audio.snapshotIntensity(name) -> number
//   audio.setBusVolume(bus, dB) / audio.busVolume(bus) -> dB after snapshots
//   audio.setBusMuted(bus, muted)
//   audio.busLevel(bus) -> loudness in dB right now
//   audio.play(entity) / audio.stop(entity) / audio.isPlaying(entity)
//   audio.setVolume(entity, volume) / audio.setPitch(entity, pitch) / audio.setBus(entity, bus)
class ScriptAudioApi {
public:
    ScriptAudioApi(Audio& audio, ECS& ecs);

    void registerInto(lua_State* L);

private:
    static int luaSnapshot(lua_State* L);
    static int luaSnapshotIntensity(lua_State* L);
    static int luaSetBusVolume(lua_State* L);
    static int luaBusVolume(lua_State* L);
    static int luaSetBusMuted(lua_State* L);
    static int luaBusLevel(lua_State* L);
    static int luaPlay(lua_State* L);
    static int luaStop(lua_State* L);
    static int luaIsPlaying(lua_State* L);
    static int luaSetVolume(lua_State* L);
    static int luaSetPitch(lua_State* L);
    static int luaSetBus(lua_State* L);

    Audio& audio_;
    ECS& ecs_;
};

} // namespace engine::core
