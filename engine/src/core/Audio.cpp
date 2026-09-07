#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#include "core/Audio.hpp"

#include <algorithm>
#include <cmath>
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
    // Real-reapplies whatever master volume was set (or left at its real
    // default of 1.0f) before initialize() ran -- see setMasterVolume()'s
    // own comment.
    ma_engine_set_volume(engine_, masterVolume_);
    return true;
}

void Audio::setMasterVolume(float volume01) {
    masterVolume_ = volume01;
    if (initialized_) ma_engine_set_volume(engine_, masterVolume_);
}

void Audio::setCategoryVolume(AudioCategory category, float volume01) {
    if (category == AudioCategory::Music) {
        musicVolume_ = volume01;
    } else {
        sfxVolume_ = volume01;
    }
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

void Audio::setSoundVolume(SoundHandle handle, float volume01) {
    if (handle >= sounds_.size() || !sounds_[handle]) return;
    ma_sound_set_volume(sounds_[handle], volume01);
}

void Audio::playFromOffset(SoundHandle handle, double offsetSeconds) {
    if (handle >= sounds_.size() || !sounds_[handle]) return;
    ma_format format;
    ma_uint32 channels = 0;
    ma_uint32 sampleRate = 0;
    if (ma_sound_get_data_format(sounds_[handle], &format, &channels, &sampleRate, nullptr, 0) != MA_SUCCESS ||
        sampleRate == 0) {
        return;
    }
    ma_uint64 frame = static_cast<ma_uint64>(offsetSeconds * static_cast<double>(sampleRate));
    ma_sound_seek_to_pcm_frame(sounds_[handle], frame);
    ma_sound_start(sounds_[handle]);
}

void Audio::stopSound(SoundHandle handle) {
    if (handle >= sounds_.size() || !sounds_[handle]) return;
    ma_sound_stop(sounds_[handle]);
}

bool Audio::isSoundPlaying(SoundHandle handle) const {
    if (handle >= sounds_.size() || !sounds_[handle]) return false;
    return ma_sound_is_playing(sounds_[handle]) == MA_TRUE;
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
        float categoryVolume = source.category == AudioCategory::Music ? musicVolume_ : sfxVolume_;
        ma_sound_set_volume(sound, source.volume * categoryVolume);
        ma_sound_set_looping(sound, source.looping ? MA_TRUE : MA_FALSE);

        bool isPlaying = ma_sound_is_playing(sound) == MA_TRUE;
        if (source.playing && !isPlaying) {
            ma_sound_start(sound);
        } else if (!source.playing && isPlaying) {
            ma_sound_stop(sound);
        }
    }
}

bool decodeAudioFileToFloatMono(const std::string& path, std::vector<float>& outSamples, uint32_t& outSampleRate) {
    ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 1, 0); // 0 sampleRate == "keep the source's own rate"
    ma_decoder decoder;
    if (ma_decoder_init_file(path.c_str(), &config, &decoder) != MA_SUCCESS) return false;

    ma_uint64 frameCount = 0;
    if (ma_decoder_get_length_in_pcm_frames(&decoder, &frameCount) != MA_SUCCESS || frameCount == 0) {
        ma_decoder_uninit(&decoder);
        return false;
    }

    std::vector<float> samples(static_cast<size_t>(frameCount));
    ma_uint64 framesRead = 0;
    ma_result result = ma_decoder_read_pcm_frames(&decoder, samples.data(), frameCount, &framesRead);
    uint32_t sampleRate = decoder.outputSampleRate;
    ma_decoder_uninit(&decoder);
    if (result != MA_SUCCESS && result != MA_AT_END) return false;

    samples.resize(static_cast<size_t>(framesRead));
    outSamples = std::move(samples);
    outSampleRate = sampleRate;
    return true;
}

bool encodeFloatMonoToWavFile(const std::string& path, const std::vector<float>& samples, uint32_t sampleRate) {
    if (samples.empty() || sampleRate == 0) return false;

    ma_encoder_config config = ma_encoder_config_init(ma_encoding_format_wav, ma_format_f32, 1, sampleRate);
    ma_encoder encoder;
    if (ma_encoder_init_file(path.c_str(), &config, &encoder) != MA_SUCCESS) return false;

    ma_uint64 framesWritten = 0;
    ma_result result = ma_encoder_write_pcm_frames(&encoder, samples.data(), samples.size(), &framesWritten);
    ma_encoder_uninit(&encoder);
    return result == MA_SUCCESS && framesWritten == samples.size();
}

std::vector<std::pair<float, float>> computeWaveformPeaks(const std::vector<float>& samples, size_t bucketCount) {
    std::vector<std::pair<float, float>> peaks;
    if (samples.empty() || bucketCount == 0) return peaks;
    peaks.resize(bucketCount);

    // Each bucket's [start, end) is derived straight from its own index
    // over samples.size()/bucketCount, not a fixed floor(N/bucketCount)
    // stride -- a fixed stride under-covers every bucket by the same
    // truncated remainder and then dumps the entire accumulated shortfall
    // onto the last one (e.g. N=1023, bucketCount=512: stride=1, so
    // buckets 0..510 get exactly 1 sample each while bucket 511 alone
    // absorbs the other 512 -- half the clip collapses into one bar).
    // Deriving start/end from `i` and `i+1` spreads that remainder evenly
    // across buckets instead, and both ends are real sample indices, so
    // there's no "ran out of samples" case left to special-case.
    for (size_t i = 0; i < bucketCount; ++i) {
        size_t start = i * samples.size() / bucketCount;
        size_t end = (i + 1) * samples.size() / bucketCount;
        if (end <= start) end = start + 1;
        end = std::min(end, samples.size());
        float minVal = samples[start];
        float maxVal = samples[start];
        for (size_t s = start; s < end; ++s) {
            minVal = std::min(minVal, samples[s]);
            maxVal = std::max(maxVal, samples[s]);
        }
        peaks[i] = {minVal, maxVal};
    }
    return peaks;
}

namespace {
constexpr float kSilenceFloorDbfs = -100.0f;

float linearToDbfs(float linear) {
    return linear > 0.0f ? 20.0f * std::log10(linear) : kSilenceFloorDbfs;
}
} // namespace

float computePeakDbfs(const std::vector<float>& samples) {
    float peak = 0.0f;
    for (float s : samples) peak = std::max(peak, std::fabs(s));
    return std::max(kSilenceFloorDbfs, linearToDbfs(peak));
}

float computeRmsDbfs(const std::vector<float>& samples) {
    if (samples.empty()) return kSilenceFloorDbfs;
    double sumSquares = 0.0;
    for (float s : samples) sumSquares += static_cast<double>(s) * static_cast<double>(s);
    float rms = static_cast<float>(std::sqrt(sumSquares / static_cast<double>(samples.size())));
    return std::max(kSilenceFloorDbfs, linearToDbfs(rms));
}

} // namespace engine::core
