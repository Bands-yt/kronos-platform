#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#include "core/Audio.hpp"
#include "core/Hierarchy.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace engine::core {

Audio::Audio() = default;
Audio::~Audio() { shutdown(); }

bool Audio::initialize() {
    ma_engine_config config = ma_engine_config_init();
    const char* silent = std::getenv("KRONOS_SILENT_AUDIO");
    if (silent != nullptr && silent[0] != '\0' && std::strcmp(silent, "0") != 0) {
        // Plays in real time (meters, timing) but nothing reaches the speakers.
        silentContext_ = new ma_context();
        const ma_backend nullBackend = ma_backend_null;
        if (ma_context_init(&nullBackend, 1, nullptr, silentContext_) == MA_SUCCESS) {
            config.pContext = silentContext_;
            std::fprintf(stdout, "Audio: KRONOS_SILENT_AUDIO set, using the null output device\n");
        } else {
            delete silentContext_;
            silentContext_ = nullptr;
        }
    }
    engine_ = new ma_engine();
    ma_result result = ma_engine_init(&config, engine_);
    if (result != MA_SUCCESS) {
        std::fprintf(stderr, "Audio: ma_engine_init failed (%d) -- is an audio backend (ALSA/PulseAudio) available?\n", result);
        delete engine_;
        engine_ = nullptr;
        releaseSilentContext();
        return false;
    }
    std::fprintf(stdout, "Audio: miniaudio engine initialized (%u channels @ %u Hz)\n",
                 ma_engine_get_channels(engine_), ma_engine_get_sample_rate(engine_));
    return finishInitialize();
}

bool Audio::initializeOffline(uint32_t channels, uint32_t sampleRate) {
    if (initialized_) return false;
    ma_engine_config config = ma_engine_config_init();
    config.noDevice = MA_TRUE;
    config.channels = channels;
    config.sampleRate = sampleRate;
    engine_ = new ma_engine();
    if (ma_engine_init(&config, engine_) != MA_SUCCESS) {
        delete engine_;
        engine_ = nullptr;
        return false;
    }
    return finishInitialize();
}

bool Audio::finishInitialize() {
    initialized_ = true;
    ma_engine_set_volume(engine_, masterVolume_);
    std::string error;
    if (!mixer_.attach(engine_, &error)) std::fprintf(stderr, "Audio: mixer unavailable: %s\n", error.c_str());
    return true;
}

bool Audio::renderOffline(float* interleaved, uint64_t frameCount) {
    if (!initialized_ || engine_->pDevice != nullptr) return false;
    ma_uint64 read = 0;
    return ma_engine_read_pcm_frames(engine_, interleaved, frameCount, &read) == MA_SUCCESS && read == frameCount;
}

uint32_t Audio::channels() const { return initialized_ ? ma_engine_get_channels(engine_) : 0; }

uint32_t Audio::sampleRate() const { return initialized_ ? ma_engine_get_sample_rate(engine_) : 0; }

void Audio::setSoundBus(SoundHandle handle, const std::string& bus) {
    if (handle >= sounds_.size() || !sounds_[handle]) return;
    mixer_.route(sounds_[handle], bus);
}

std::string Audio::soundBus(SoundHandle handle) const {
    if (handle >= sounds_.size() || !sounds_[handle]) return {};
    return mixer_.busOf(sounds_[handle]);
}

void Audio::update(float dt) { mixer_.update(dt); }

void Audio::setMasterVolume(float volume01) {
    masterVolume_ = volume01;
    if (initialized_) ma_engine_set_volume(engine_, masterVolume_);
}

void Audio::setCategoryVolume(AudioCategory category, float volume01) {
    mixer_.setUserVolume(category == AudioCategory::Music ? "Music" : "SFX", volume01);
}

void Audio::shutdown() {
    if (!initialized_) return;

    releaseVoices();
    for (SoundHandle handle = 0; handle < sounds_.size(); ++handle) unloadSound(handle);
    sounds_.clear();
    buffers_.clear();
    mixer_.detach();

    ma_engine_uninit(engine_);
    delete engine_;
    engine_ = nullptr;
    releaseSilentContext();
    initialized_ = false;
}

void Audio::releaseSilentContext() {
    if (silentContext_ == nullptr) return;
    ma_context_uninit(silentContext_);
    delete silentContext_;
    silentContext_ = nullptr;
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
    buffers_.push_back(nullptr);
    mixer_.route(sound, {});
    return static_cast<SoundHandle>(sounds_.size() - 1);
}

void Audio::unloadSound(SoundHandle handle) {
    if (handle >= sounds_.size()) return;
    releaseVoicesOf(handle, false);
    if (sounds_[handle]) {
        mixer_.forget(sounds_[handle]);
        ma_sound_uninit(sounds_[handle]);
        delete sounds_[handle];
        sounds_[handle] = nullptr;
    }
    if (handle < buffers_.size() && buffers_[handle]) {
        ma_audio_buffer_uninit_and_free(static_cast<ma_audio_buffer*>(buffers_[handle]));
        buffers_[handle] = nullptr;
    }
}

SoundHandle Audio::reserveSound() {
    sounds_.push_back(nullptr);
    buffers_.push_back(nullptr);
    return static_cast<SoundHandle>(sounds_.size() - 1);
}

bool Audio::setSoundPcm(SoundHandle handle, const float* interleaved, uint64_t frameCount, uint32_t channels,
                        uint32_t sampleRate) {
    if (!initialized_ || handle >= sounds_.size() || interleaved == nullptr || frameCount == 0 || channels == 0 ||
        sampleRate == 0) {
        return false;
    }

    ma_audio_buffer_config bufferConfig = ma_audio_buffer_config_init(ma_format_f32, channels, frameCount, interleaved, nullptr);
    bufferConfig.sampleRate = sampleRate;
    ma_audio_buffer* buffer = nullptr;
    if (ma_audio_buffer_alloc_and_init(&bufferConfig, &buffer) != MA_SUCCESS) return false;

    auto* sound = new ma_sound();
    if (ma_sound_init_from_data_source(engine_, buffer, 0, nullptr, sound) != MA_SUCCESS) {
        delete sound;
        ma_audio_buffer_uninit_and_free(buffer);
        return false;
    }

    double resumeSeconds = -1.0;
    std::string bus;
    if (ma_sound* old = sounds_[handle]) {
        bus = mixer_.busOf(old);
        ma_sound_set_looping(sound, ma_sound_is_looping(old));
        ma_sound_set_volume(sound, ma_sound_get_volume(old));
        ma_sound_set_pitch(sound, ma_sound_get_pitch(old));
        ma_sound_set_spatialization_enabled(sound, ma_sound_is_spatialization_enabled(old));
        ma_sound_set_min_distance(sound, ma_sound_get_min_distance(old));
        ma_sound_set_max_distance(sound, ma_sound_get_max_distance(old));
        const ma_vec3f position = ma_sound_get_position(old);
        ma_sound_set_position(sound, position.x, position.y, position.z);
        if (ma_sound_is_playing(old) == MA_TRUE) {
            float cursor = 0.0f;
            if (ma_sound_get_cursor_in_seconds(old, &cursor) == MA_SUCCESS) resumeSeconds = cursor;
        }
    }
    releaseVoicesOf(handle, true);
    unloadSound(handle);
    sounds_[handle] = sound;
    buffers_[handle] = buffer;
    mixer_.route(sound, bus);

    if (resumeSeconds >= 0.0) {
        const double lengthSeconds = static_cast<double>(frameCount) / sampleRate;
        if (resumeSeconds < lengthSeconds || ma_sound_is_looping(sound)) {
            const auto frame = static_cast<ma_uint64>(std::fmod(resumeSeconds, lengthSeconds) * sampleRate);
            ma_sound_seek_to_pcm_frame(sound, frame);
            ma_sound_start(sound);
        }
    }
    return true;
}

void Audio::playOneShot(SoundHandle handle) {
    if (handle >= sounds_.size() || !sounds_[handle]) return;
    // Overlapping calls on one handle restart it; entity sounds get their own voices in mix().
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

void Audio::setSoundLooping(SoundHandle handle, bool looping) {
    if (handle >= sounds_.size() || !sounds_[handle]) return;
    ma_sound_set_looping(sounds_[handle], looping ? MA_TRUE : MA_FALSE);
}

void Audio::setSoundSpatialized(SoundHandle handle, bool spatialized) {
    if (handle >= sounds_.size() || !sounds_[handle]) return;
    ma_sound_set_spatialization_enabled(sounds_[handle], spatialized ? MA_TRUE : MA_FALSE);
}

void Audio::setSoundPitch(SoundHandle handle, float pitch) {
    if (handle >= sounds_.size() || !sounds_[handle]) return;
    ma_sound_set_pitch(sounds_[handle], std::max(pitch, 0.01f));
}

bool Audio::isSoundPlaying(SoundHandle handle) const {
    if (handle >= sounds_.size() || !sounds_[handle]) return false;
    return ma_sound_is_playing(sounds_[handle]) == MA_TRUE;
}

ma_sound* Audio::makeVoice(SoundHandle handle, void*& bufferRef) {
    bufferRef = nullptr;
    auto* voice = new ma_sound();
    if (auto* buffer = static_cast<ma_audio_buffer*>(buffers_[handle])) {
        auto* ref = new ma_audio_buffer_ref();
        if (ma_audio_buffer_ref_init(buffer->ref.format, buffer->ref.channels, buffer->ref.pData, buffer->ref.sizeInFrames,
                                     ref) != MA_SUCCESS) {
            delete ref;
            delete voice;
            return nullptr;
        }
        ref->sampleRate = buffer->ref.sampleRate;
        if (ma_sound_init_from_data_source(engine_, ref, 0, nullptr, voice) != MA_SUCCESS) {
            ma_audio_buffer_ref_uninit(ref);
            delete ref;
            delete voice;
            return nullptr;
        }
        bufferRef = ref;
    } else if (ma_sound_init_copy(engine_, sounds_[handle], 0, nullptr, voice) != MA_SUCCESS) {
        delete voice;
        return nullptr;
    }
    return voice;
}

void Audio::destroyVoice(Voice& voice) {
    if (voice.sound) {
        mixer_.forget(voice.sound);
        ma_sound_uninit(voice.sound);
        delete voice.sound;
        voice.sound = nullptr;
    }
    if (auto* ref = static_cast<ma_audio_buffer_ref*>(voice.bufferRef)) {
        ma_audio_buffer_ref_uninit(ref);
        delete ref;
        voice.bufferRef = nullptr;
    }
}

void Audio::releaseVoicesOf(SoundHandle handle, bool keepCursor) {
    for (auto it = voices_.begin(); it != voices_.end();) {
        if (it->second.handle != handle) {
            ++it;
            continue;
        }
        float cursor = 0.0f;
        if (keepCursor && ma_sound_is_playing(it->second.sound) == MA_TRUE &&
            ma_sound_get_cursor_in_seconds(it->second.sound, &cursor) == MA_SUCCESS) {
            voiceResume_[it->first] = cursor;
        }
        destroyVoice(it->second);
        it = voices_.erase(it);
    }
}

void Audio::releaseVoices() {
    for (auto& [entity, voice] : voices_) destroyVoice(voice);
    voices_.clear();
    voiceResume_.clear();
}

bool Audio::isEntitySoundPlaying(EntityId entity) const {
    const auto it = voices_.find(entity);
    return it != voices_.end() && ma_sound_is_playing(it->second.sound) == MA_TRUE;
}

std::string Audio::entitySoundBus(EntityId entity) const {
    const auto it = voices_.find(entity);
    return it != voices_.end() ? mixer_.busOf(it->second.sound) : std::string{};
}

void Audio::mix(ECS& ecs, glm::vec3 listenerPosition, glm::vec3 listenerForward, glm::vec3 listenerUp) {
    if (!initialized_) return;
    ++mixCount_;

    ma_engine_listener_set_position(engine_, 0, listenerPosition.x, listenerPosition.y, listenerPosition.z);
    ma_engine_listener_set_direction(engine_, 0, listenerForward.x, listenerForward.y, listenerForward.z);
    ma_engine_listener_set_world_up(engine_, 0, listenerUp.x, listenerUp.y, listenerUp.z);

    auto view = ecs.view<AudioSource, Transform>();
    for (auto entity : view) {
        auto& source = view.get<AudioSource>(entity);
        if (source.soundHandle == AudioSource::kInvalidHandle || source.soundHandle >= sounds_.size()) continue;
        ma_sound* base = sounds_[source.soundHandle];
        if (!base) continue;

        auto found = voices_.find(entity);
        if (found != voices_.end() && (found->second.handle != source.soundHandle || found->second.base != base)) {
            destroyVoice(found->second);
            voices_.erase(found);
            found = voices_.end();
        }
        if (found == voices_.end()) {
            Voice voice;
            voice.sound = makeVoice(source.soundHandle, voice.bufferRef);
            if (!voice.sound) continue;
            voice.handle = source.soundHandle;
            voice.base = base;
            found = voices_.emplace(entity, voice).first;
            if (const auto resume = voiceResume_.find(entity); resume != voiceResume_.end()) {
                if (source.playing) ma_sound_seek_to_second(voice.sound, static_cast<float>(resume->second));
                voiceResume_.erase(resume);
            }
        }
        found->second.lastMix = mixCount_;
        ma_sound* sound = found->second.sound;

        glm::vec3 position = view.get<Transform>(entity).position;
        if (const auto* h = ecs.tryGetComponent<Hierarchy>(entity); h != nullptr && h->parent != entt::null) {
            position = glm::vec3(hierarchy::computeWorldMatrix(ecs, entity)[3]);
        }
        ma_sound_set_position(sound, position.x, position.y, position.z);
        ma_sound_set_min_distance(sound, source.minDistance);
        ma_sound_set_max_distance(sound, source.maxDistance);
        ma_sound_set_volume(sound, source.volume);
        ma_sound_set_pitch(sound, std::max(source.pitch, 0.01f));
        ma_sound_set_spatialization_enabled(sound, source.spatial ? MA_TRUE : MA_FALSE);
        ma_sound_set_looping(sound, source.looping ? MA_TRUE : MA_FALSE);
        mixer_.route(sound, !source.bus.empty() ? source.bus : source.category == AudioCategory::Music ? "Music" : "SFX");

        if (source.restart) {
            ma_sound_seek_to_pcm_frame(sound, 0);
            source.restart = false;
        }
        const bool isPlaying = ma_sound_is_playing(sound) == MA_TRUE;
        if (source.playing && !isPlaying) {
            if (!source.looping && ma_sound_at_end(sound) == MA_TRUE) {
                source.playing = false;
            } else {
                ma_sound_start(sound);
            }
        } else if (!source.playing && isPlaying) {
            ma_sound_stop(sound);
        }
    }

    // Voices whose entity is gone or lost its sound.
    for (auto it = voices_.begin(); it != voices_.end();) {
        if (it->second.lastMix == mixCount_) {
            ++it;
            continue;
        }
        destroyVoice(it->second);
        it = voices_.erase(it);
    }
}

bool decodeAudioFileToFloat(const std::string& path, std::vector<float>& outInterleaved, uint32_t& outChannels,
                            uint32_t& outSampleRate, std::string* error) {
    ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 0, 0);
    ma_decoder decoder;
    if (ma_decoder_init_file(path.c_str(), &config, &decoder) != MA_SUCCESS) {
        if (error) *error = "unsupported or unreadable audio file";
        return false;
    }
    const uint32_t channels = decoder.outputChannels;
    std::vector<float> samples;
    constexpr ma_uint64 kChunkFrames = 16384;
    for (;;) {
        const size_t offset = samples.size();
        samples.resize(offset + kChunkFrames * channels);
        ma_uint64 framesRead = 0;
        const ma_result result = ma_decoder_read_pcm_frames(&decoder, samples.data() + offset, kChunkFrames, &framesRead);
        samples.resize(offset + framesRead * channels);
        if (result == MA_AT_END || framesRead < kChunkFrames) break;
        if (result != MA_SUCCESS) {
            ma_decoder_uninit(&decoder);
            if (error) *error = "decode error";
            return false;
        }
    }
    outSampleRate = decoder.outputSampleRate;
    ma_decoder_uninit(&decoder);
    if (samples.empty() || channels == 0) {
        if (error) *error = "audio file has no samples";
        return false;
    }
    outChannels = channels;
    outInterleaved = std::move(samples);
    return true;
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
