#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "core/ECS.hpp"

// Opaque forward declaration -- miniaudio.h (and its MINIAUDIO_IMPLEMENTATION
// translation unit) are only included in Audio.cpp, so nothing else in the
// engine has to compile against a 90k-line single header.
struct ma_engine;
struct ma_sound;

namespace engine::core {

using SoundHandle = uint32_t;
inline constexpr SoundHandle kInvalidSoundHandle = ~0u;

// Wraps miniaudio per docs/ARCHITECTURE.md §4.3: streaming decode for
// music/ambience, one-shot decode for short SFX, mixed 3D-positionally from
// AudioSource + Transform each frame -- matching how Roblox's own
// Sound/SoundService model already expects audio to behave (so the
// Scripting-layer Sound wrapper, once built, is a thin pass-through here
// rather than a redesign).
class Audio {
public:
    Audio();
    ~Audio();

    Audio(const Audio&) = delete;
    Audio& operator=(const Audio&) = delete;

    [[nodiscard]] bool initialize();
    void shutdown();

    // Decodes fully into memory -- appropriate for short SFX. Streaming
    // playback (music/ambience) is the same call with
    // MA_SOUND_FLAG_STREAM, exposed once ModuleScript-driven asset loading
    // (§7 migration asset converter) has real paths to hand it.
    [[nodiscard]] SoundHandle loadSound(const std::string& path);
    void unloadSound(SoundHandle handle);

    // Fire-and-forget, non-positional playback (UI sounds, StarterGui click
    // feedback, etc).
    void playOneShot(SoundHandle handle);

    // Per-frame mix: updates the listener (camera/character) and every
    // AudioSource-tagged entity's spatialization, matching whichever of
    // playing/looping the ECS component currently says. This is the "3D
    // positional audio stub" -- real distance-attenuation curves and
    // occlusion are gameplay/Studio-tunable (AudioSource::minDistance /
    // maxDistance) rather than hardcoded further than miniaudio's defaults.
    void mix(ECS& ecs, glm::vec3 listenerPosition, glm::vec3 listenerForward, glm::vec3 listenerUp);

    // Kronos ("Settings Panel v2 + Input Remapping + Accessibility
    // Layer" -- "Audio: Master volume"): real, immediate -- forwards
    // directly to miniaudio's own real engine-level volume
    // (ma_engine_set_volume), applied on top of every category's own
    // volume and every individual AudioSource::volume, matching how a
    // real master fader always sits above per-bus faders. A real,
    // honest no-op before initialize() succeeds (nothing to set volume
    // on yet) -- the caller's own real, chosen value is remembered
    // (masterVolume_) and reapplied by initialize() so a setting applied
    // before startNetworking()/audio init still takes effect once audio
    // actually comes up.
    void setMasterVolume(float volume01);
    // Kronos ("Settings Panel v2 + Input Remapping + Accessibility
    // Layer" -- "Audio: Music volume, SFX volume"): real, immediate --
    // takes effect on this category's every AudioSource the very next
    // mix() call (no per-sound bookkeeping needed since mix() already
    // re-applies every AudioSource's own volume every frame).
    void setCategoryVolume(AudioCategory category, float volume01);

private:
    ma_engine* engine_ = nullptr;
    std::vector<ma_sound*> sounds_;
    bool initialized_ = false;
    float masterVolume_ = 1.0f;
    float musicVolume_ = 1.0f;
    float sfxVolume_ = 1.0f;
};

} // namespace engine::core
