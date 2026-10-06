#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include "core/AudioMixer.hpp"
#include "core/ECS.hpp"

// Opaque forward declaration -- miniaudio.h (and its MINIAUDIO_IMPLEMENTATION
// translation unit) are only included in Audio.cpp, so nothing else in the
// engine has to compile against a 90k-line single header.
struct ma_context;
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
    // No audio device: renderOffline() pulls the mix instead. Used by tests
    // and for rendering audio to a file.
    [[nodiscard]] bool initializeOffline(uint32_t channels, uint32_t sampleRate);
    void shutdown();
    // Mixes the next frameCount frames into `interleaved` (offline only).
    bool renderOffline(float* interleaved, uint64_t frameCount);
    [[nodiscard]] uint32_t channels() const;
    [[nodiscard]] uint32_t sampleRate() const;

    // Buses, sends, ducking and snapshots (see AudioMixer.hpp). Every
    // sound starts on the master bus.
    [[nodiscard]] AudioMixer& mixer() { return mixer_; }
    [[nodiscard]] const AudioMixer& mixer() const { return mixer_; }
    void setSoundBus(SoundHandle handle, const std::string& bus);
    [[nodiscard]] std::string soundBus(SoundHandle handle) const;
    // Advances snapshot fades.
    void update(float dt);

    // Decodes fully into memory -- appropriate for short SFX. Streaming
    // playback (music/ambience) is the same call with
    // MA_SOUND_FLAG_STREAM, exposed once ModuleScript-driven asset loading
    // (§7 migration asset converter) has real paths to hand it.
    [[nodiscard]] SoundHandle loadSound(const std::string& path);
    void unloadSound(SoundHandle handle);

    // A silent slot that setSoundPcm() fills later; every call on it is a
    // no-op until then. Used by the resource layer to hand out a handle
    // before the file has finished decoding on a worker.
    [[nodiscard]] SoundHandle reserveSound();
    // Creates or replaces the sound at `handle` from interleaved float
    // PCM. Replacing keeps looping, volume, pitch and spatialization, and
    // a sound that was playing resumes from the same time.
    bool setSoundPcm(SoundHandle handle, const float* interleaved, uint64_t frameCount, uint32_t channels,
                     uint32_t sampleRate);
    [[nodiscard]] bool isInitialized() const { return initialized_; }

    // Fire-and-forget, non-positional playback (UI sounds, StarterGui click
    // feedback, etc).
    void playOneShot(SoundHandle handle);

    // Kronos ("CapCut/DaVinci Hybrid NLE Suite" -- real ClipTimeline
    // playback): seeks to `offsetSeconds` into the sound's own real
    // decoded data (via the sound's own native sample rate --
    // ma_sound_seek_to_pcm_frame operates in the data source's own frame
    // count, not the shared engine output rate miniaudio resamples to on
    // mix) and starts it -- what a trimmed clip (MediaClip::
    // sourceOffsetSeconds > 0) needs to play the right part of its
    // source file, instead of always restarting at 0:00 like
    // playOneShot() does. A real, honest no-op on an invalid handle or a
    // sound whose data format can't be queried.
    void playFromOffset(SoundHandle handle, double offsetSeconds);

    // Real, immediate stop (ma_sound_stop) -- the other half
    // playFromOffset() needs: a clip's sound must stop the instant the
    // playhead scrubs back out of its span, not keep playing to its own
    // natural end. A real, honest no-op on an invalid/already-stopped
    // handle.
    void stopSound(SoundHandle handle);
    void setSoundLooping(SoundHandle handle, bool looping);
    // Non-spatialized sounds play at full volume regardless of the listener (UI, first-person SFX).
    void setSoundSpatialized(SoundHandle handle, bool spatialized);
    // 1.0 is the recorded pitch; also changes playback speed.
    void setSoundPitch(SoundHandle handle, float pitch);

    // Real, honest read of whether this handle is currently playing
    // (ma_sound_is_playing) -- lets a caller (NleTimelinePlugin's own
    // playback driver) avoid calling playFromOffset() again every single
    // frame the playhead sits inside the same clip.
    [[nodiscard]] bool isSoundPlaying(SoundHandle handle) const;

    // Kronos ("Audio Track Mixer" -- kronos_audio's own dedicated
    // workspace): real, immediate per-sound volume (ma_sound_set_volume),
    // for a plain loadSound()/playOneShot() handle -- distinct from
    // AudioSource::volume (an ECS-driven, per-entity field mix() applies
    // every frame; a preview sound played via playOneShot() has no
    // AudioSource component at all). A real, honest no-op on an
    // unloaded/invalid handle, same "range-check, don't assert" contract
    // unloadSound()/playOneShot() above already use.
    void setSoundVolume(SoundHandle handle, float volume01);

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
    // The player's volume setting for the Music or SFX bus.
    void setCategoryVolume(AudioCategory category, float volume01);

private:
    bool finishInitialize();
    void releaseSilentContext();

    ma_context* silentContext_ = nullptr; // set when KRONOS_SILENT_AUDIO=1
    ma_engine* engine_ = nullptr;
    AudioMixer mixer_;
    std::vector<ma_sound*> sounds_;
    std::vector<void*> buffers_; // ma_audio_buffer*, parallel to sounds_, set for PCM-backed sounds
    bool initialized_ = false;
    float masterVolume_ = 1.0f;
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
// Full decode at the file's own channel count and sample rate,
// interleaved. Safe to call from any thread.
[[nodiscard]] bool decodeAudioFileToFloat(const std::string& path, std::vector<float>& outInterleaved,
                                          uint32_t& outChannels, uint32_t& outSampleRate, std::string* error = nullptr);

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

// Kronos ("Waveform Inspector" -- kronos_audio's own dedicated
// workspace): one min/max pair per bucket, computed once over the whole
// (potentially millions-of-samples-long) real decoded buffer -- real
// PCM data, the same buffer decodeAudioFileToFloatMono() already
// produces for the DSP graph, not a synthetic/placeholder waveform. A
// pure, real, unit-testable function (no ImGui/miniaudio dependency)
// specifically so studio::plugins::AudioPreviewPlugin can call this once
// at Load time and cache the result, instead of re-reducing the raw
// buffer every single frame it draws (a multi-minute clip is millions of
// samples -- a per-frame reduction over that would visibly tank the
// frame rate). Returns one entry per bucket regardless of `samples`'
// length (the last bucket absorbs any remainder from an uneven split);
// an empty `samples` or bucketCount==0 returns an empty result, a real,
// honest "nothing to draw", not a divide-by-zero.
[[nodiscard]] std::vector<std::pair<float, float>> computeWaveformPeaks(const std::vector<float>& samples,
                                                                         size_t bucketCount);

// Kronos ("Audio Track Mixer" -- kronos_audio's own dedicated
// workspace): real peak/RMS level in dBFS (decibels relative to full
// scale, 0 dB = the loudest a float sample can represent without
// clipping) over a whole real decoded buffer -- the same real, honest
// "static analysis of the actual clip" scope as computeWaveformPeaks()
// above, not a live-updating VU needle (this engine's Audio API has no
// per-frame playback-cursor readback to drive one, see
// core::Audio::playOneShot()'s own comment on why one ma_sound instance
// is reused rather than tracked frame-by-frame). Silence (every sample
// exactly 0, including an empty buffer) returns a real, finite floor
// (-100 dB) rather than -infinity, so the mixer's own meter bar math
// never has to special-case it.
[[nodiscard]] float computePeakDbfs(const std::vector<float>& samples);
[[nodiscard]] float computeRmsDbfs(const std::vector<float>& samples);

} // namespace engine::core
