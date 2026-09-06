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

// Kronos ("Node-Based Audio DSP" -- v0.4.0 Creator Suite): real, full-file
// decode into an in-memory mono float32 buffer -- the raw-sample access
// core::AudioDspGraph/core::PhonemeLipSync both need and that
// core::Audio's own ma_sound-based playback path never exposes (a
// ma_sound plays through miniaudio's own internal pipeline; it never
// hands the caller a plain sample array). Real, separate ma_decoder
// instance from AssetMetadata.cpp's own probe -- same miniaudio API,
// used here to actually read PCM frames rather than just inspect
// length/format. Forces mono (channels=1) since both DSP-graph
// processing and dialogue-viseme extraction are real, honest,
// single-channel operations here -- a stereo source is downmixed by
// miniaudio's own real channel converter, not a silent stereo-only bug.
// Returns false (and leaves outSamples/outSampleRate untouched) if the
// file can't be opened or decoded -- the same "real, honest failure,
// not a partial result" contract Texture::loadFromFile() already uses.
[[nodiscard]] bool decodeAudioFileToFloatMono(const std::string& path, std::vector<float>& outSamples,
                                               uint32_t& outSampleRate);

// Kronos ("Node-Based Audio DSP"): the real write-back half of
// decodeAudioFileToFloatMono() -- real miniaudio ma_encoder (mono
// 32-bit float WAV), used by
// studio::plugins::AudioPreviewPlugin to turn an core::AudioDspGraph's
// processed buffer into a real, playable file (core::Audio::loadSound()
// only ever loads from a real file path -- there is no in-memory sound
// source in this engine's Audio API). "Non-destructive" per
// AudioDspGraph.hpp's own header comment: this always writes a NEW
// file, never overwrites `path`'s own original source. Returns false
// on any real encoder failure (bad path, unwritable directory) -- an
// honest failure, not a partial file.
[[nodiscard]] bool encodeFloatMonoToWavFile(const std::string& path, const std::vector<float>& samples,
                                             uint32_t sampleRate);

} // namespace engine::core
