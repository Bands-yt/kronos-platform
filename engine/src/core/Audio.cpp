#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#include "core/Audio.hpp"

#include <cstdio>

namespace engine::core {

Audio::Audio() = default;
Audio::~Audio() { shutdown(); }

bool Audio::initialize() {
    engine_ = new ma_engine();
    ma_result result = ma_engine_init(nullptr, engine_);
    if (result != MA_SUCCESS) {
        std::fprintf(stderr, "Audio: ma_engine_init failed (%d) -- is an audio backend (ALSA/PulseAudio) available?\n", result);
        delete engine_;
        engine_ = nullptr;
        return false;
    }
    initialized_ = true;
    std::fprintf(stdout, "Audio: miniaudio engine initialized (%u channels @ %u Hz)\n",
                 ma_engine_get_channels(engine_), ma_engine_get_sample_rate(engine_));
    return true;
}

void Audio::shutdown() {
    if (!initialized_) return;

    for (ma_sound* sound : sounds_) {
        if (sound) {
            ma_sound_uninit(sound);
            delete sound;
        }
    }
    sounds_.clear();

    ma_engine_uninit(engine_);
    delete engine_;
    engine_ = nullptr;
    initialized_ = false;
}

SoundHandle Audio::loadSound(const std::string& path) {
    if (!initialized_) return kInvalidSoundHandle;

    auto* sound = new ma_sound();
    ma_result result = ma_sound_init_from_file(engine_, path.c_str(), 0, nullptr, nullptr, sound);
    if (result != MA_SUCCESS) {
        std::fprintf(stderr, "Audio: failed to load \"%s\" (%d)\n", path.c_str(), result);
        delete sound;
        return kInvalidSoundHandle;
    }

    sounds_.push_back(sound);
    return static_cast<SoundHandle>(sounds_.size() - 1);
}

void Audio::unloadSound(SoundHandle handle) {
    if (handle >= sounds_.size() || !sounds_[handle]) return;
    ma_sound_uninit(sounds_[handle]);
    delete sounds_[handle];
    sounds_[handle] = nullptr;
}

void Audio::playOneShot(SoundHandle handle) {
    if (handle >= sounds_.size() || !sounds_[handle]) return;
    // NOTE: reusing a single ma_sound instance means overlapping
    // playOneShot() calls on the same handle cut each other off. A real
    // SFX pool would clone `sounds_[handle]` per play via
    // ma_sound_init_copy(); left as the obvious next step rather than
    // built out here, since nothing yet calls this concurrently enough to
    // need it.
    ma_sound_seek_to_pcm_frame(sounds_[handle], 0);
    ma_sound_start(sounds_[handle]);
}

void Audio::mix(ECS& ecs, glm::vec3 listenerPosition, glm::vec3 listenerForward, glm::vec3 listenerUp) {
    if (!initialized_) return;

    ma_engine_listener_set_position(engine_, 0, listenerPosition.x, listenerPosition.y, listenerPosition.z);
    ma_engine_listener_set_direction(engine_, 0, listenerForward.x, listenerForward.y, listenerForward.z);
    ma_engine_listener_set_world_up(engine_, 0, listenerUp.x, listenerUp.y, listenerUp.z);

    auto view = ecs.view<AudioSource, Transform>();
    for (auto entity : view) {
        auto& source = view.get<AudioSource>(entity);
        if (source.soundHandle == AudioSource::kInvalidHandle || source.soundHandle >= sounds_.size()) continue;
        ma_sound* sound = sounds_[source.soundHandle];
        if (!sound) continue;

        auto& transform = view.get<Transform>(entity);
        ma_sound_set_position(sound, transform.position.x, transform.position.y, transform.position.z);
        ma_sound_set_min_distance(sound, source.minDistance);
        ma_sound_set_max_distance(sound, source.maxDistance);
        ma_sound_set_volume(sound, source.volume);
        ma_sound_set_looping(sound, source.looping ? MA_TRUE : MA_FALSE);

        bool isPlaying = ma_sound_is_playing(sound) == MA_TRUE;
        if (source.playing && !isPlaying) {
            ma_sound_start(sound);
        } else if (!source.playing && isPlaying) {
            ma_sound_stop(sound);
        }
    }
}

} // namespace engine::core
