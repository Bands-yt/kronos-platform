#include "brokenbones/Music.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <random>

#include "brokenbones/SoundBank.hpp"

namespace engine::brokenbones {

namespace {

constexpr float kPi = 3.14159265f;
constexpr float kRate = static_cast<float>(kSfxSampleRate);
constexpr int kBars = 8;

float noteHz(int semitonesFromA2) { return 110.0f * std::exp2(static_cast<float>(semitonesFromA2) / 12.0f); }

class Wavetable {
public:
    // amplitude(h) for harmonics 1..count
    template <typename F>
    Wavetable(int count, F amplitude) : table_(kSize + 1) {
        for (size_t i = 0; i < kSize; ++i) {
            float phase = static_cast<float>(i) / kSize;
            float sum = 0.0f;
            for (int h = 1; h <= count; ++h) sum += amplitude(h) * std::sin(2.0f * kPi * static_cast<float>(h) * phase);
            table_[i] = sum;
        }
        float peak = 0.0f;
        for (float v : table_) peak = std::max(peak, std::abs(v));
        for (float& v : table_) v /= std::max(peak, 1e-6f);
        table_[kSize] = table_[0];
    }
    [[nodiscard]] float at(double phase) const {
        double wrapped = phase - std::floor(phase);
        double x = wrapped * kSize;
        auto i = static_cast<size_t>(x);
        float frac = static_cast<float>(x - static_cast<double>(i));
        return table_[i] + (table_[i + 1] - table_[i]) * frac;
    }

private:
    static constexpr size_t kSize = 2048;
    std::vector<float> table_;
};

struct Envelope {
    float attack;
    float decay;   // per second, towards sustain
    float sustain;
    float release; // seconds after the note ends
};

struct Voice {
    const Wavetable* wave;
    Envelope env;
    float cutoffHz = 0.0f;     // 0 = unfiltered
    float cutoffEnvHz = 0.0f;  // added to the cutoff at full envelope
    float detune = 0.0f;       // second oscillator, as a frequency ratio offset
};

void addNote(std::vector<float>& out, const Voice& voice, float hz, double startSeconds, float lengthSeconds, float gain) {
    auto start = static_cast<size_t>(std::llround(startSeconds * kRate));
    size_t total = static_cast<size_t>((lengthSeconds + voice.env.release) * kRate);
    float lp1 = 0.0f;
    float lp2 = 0.0f;
    for (size_t i = 0; i < total && start + i < out.size(); ++i) {
        float t = static_cast<float>(i) / kRate;
        const Envelope& e = voice.env;
        float amp = t < e.attack ? t / e.attack : e.sustain + (1.0f - e.sustain) * std::exp(-(t - e.attack) * e.decay);
        if (t > lengthSeconds) amp *= std::max(0.0f, 1.0f - (t - lengthSeconds) / e.release);
        double phase = static_cast<double>(hz) * t;
        float s = voice.wave->at(phase);
        if (voice.detune != 0.0f) s = 0.5f * (s + voice.wave->at(phase * (1.0 + voice.detune) + 0.37));
        if (voice.cutoffHz > 0.0f) {
            float cutoff = voice.cutoffHz + voice.cutoffEnvHz * amp;
            float a = 1.0f - std::exp(-2.0f * kPi * cutoff / kRate);
            lp1 += a * (s - lp1);
            lp2 += a * (lp1 - lp2);
            s = lp2;
        }
        out[start + i] += s * amp * gain;
    }
}

void addKick(std::vector<float>& out, double startSeconds, float gain) {
    auto start = static_cast<size_t>(std::llround(startSeconds * kRate));
    double phase = 0.0;
    for (size_t i = 0; i < static_cast<size_t>(0.4f * kRate) && start + i < out.size(); ++i) {
        float t = static_cast<float>(i) / kRate;
        float hz = 46.0f + 90.0f * std::exp(-t * 32.0f);
        phase += hz / kRate;
        float body = std::sin(2.0f * kPi * static_cast<float>(phase)) * std::exp(-t * 7.5f);
        float click = std::exp(-t * 500.0f) * 0.4f;
        out[start + i] += (body + click) * gain;
    }
}

void addSnare(std::vector<float>& out, double startSeconds, float gain, std::mt19937& rng) {
    std::uniform_real_distribution<float> white(-1.0f, 1.0f);
    auto start = static_cast<size_t>(std::llround(startSeconds * kRate));
    float low = 0.0f;
    for (size_t i = 0; i < static_cast<size_t>(0.3f * kRate) && start + i < out.size(); ++i) {
        float t = static_cast<float>(i) / kRate;
        float n = white(rng);
        low += (1.0f - std::exp(-2.0f * kPi * 1400.0f / kRate)) * (n - low);
        float rattle = (n - low) * std::exp(-t * 15.0f) * 0.8f;
        float tone = std::sin(2.0f * kPi * 185.0f * t) * std::exp(-t * 22.0f) * 0.6f;
        out[start + i] += (rattle + tone) * gain;
    }
}

void addHat(std::vector<float>& out, double startSeconds, float gain, std::mt19937& rng) {
    std::uniform_real_distribution<float> white(-1.0f, 1.0f);
    auto start = static_cast<size_t>(std::llround(startSeconds * kRate));
    float low = 0.0f;
    for (size_t i = 0; i < static_cast<size_t>(0.09f * kRate) && start + i < out.size(); ++i) {
        float t = static_cast<float>(i) / kRate;
        float n = white(rng);
        low += (1.0f - std::exp(-2.0f * kPi * 6500.0f / kRate)) * (n - low);
        out[start + i] += (n - low) * std::exp(-t * 60.0f) * gain;
    }
}

// Feedback echo, mixed in at `mix`.
void addEcho(std::vector<float>& bus, float delaySeconds, float feedback, float mix) {
    auto delay = static_cast<size_t>(delaySeconds * kRate);
    std::vector<float> wet(bus.size(), 0.0f);
    for (size_t i = delay; i < bus.size(); ++i) wet[i] = (bus[i - delay] + wet[i - delay]) * feedback;
    for (size_t i = 0; i < bus.size(); ++i) bus[i] += wet[i] * mix / feedback;
}

struct Chord {
    int bass;                 // semitones from A2
    std::array<int, 3> pad;   // close voicing around A3
};
constexpr Chord kAm{0, {12, 15, 19}};
constexpr Chord kF{-4, {8, 12, 15}};
constexpr Chord kC{3, {10, 15, 19}};
constexpr Chord kG{-2, {10, 14, 17}};
constexpr Chord kE{-5, {11, 14, 19}};
constexpr Chord kDm{5, {8, 12, 17}};

struct Style {
    float bpm;
    std::array<Chord, kBars> bars;
    float padGain;
    int arpSteps; // per bar
    float arpGain;
    bool arpBright;
    int bassSteps;
    float bassGain;
    float kickGain;
    bool fourOnFloor;
    float snareGain;
    int hatSteps;
    float hatGain;
    float echoMix;
};

Style styleFor(Track track) {
    switch (track) {
        case Track::Title:
            return {80.0f, {kAm, kF, kC, kG, kAm, kF, kDm, kE}, 0.55f, 8, 0.2f, false, 1, 0.4f,
                    0.0f, false, 0.0f, 0, 0.0f, 0.4f};
        case Track::Cliff:
            return {104.0f, {kAm, kF, kC, kG, kAm, kF, kG, kE}, 0.32f, 16, 0.13f, false, 8, 0.38f,
                    0.75f, false, 0.32f, 8, 0.1f, 0.25f};
        case Track::Freefall:
            return {144.0f, {kAm, kAm, kF, kG, kAm, kAm, kF, kE}, 0.24f, 16, 0.15f, true, 16, 0.42f,
                    0.9f, true, 0.5f, 16, 0.11f, 0.15f};
    }
    return styleFor(Track::Cliff);
}

void renderPass(const Style& style, double offset, std::vector<float>& pads, std::vector<float>& arp,
                std::vector<float>& bass, std::vector<float>& drums, std::mt19937& rng) {
    static const Wavetable kWarm(10, [](int h) { return std::pow(static_cast<float>(h), -1.6f); });
    static const Wavetable kSaw(24, [](int h) { return 1.0f / static_cast<float>(h); });
    static const Wavetable kPluck(9, [](int h) { return h % 2 == 1 ? 1.0f / static_cast<float>(h * h) : 0.0f; });
    const Voice padVoice{&kWarm, {0.6f, 0.4f, 0.75f, 0.9f}, 0.0f, 0.0f, 0.004f};
    const Voice arpVoice{style.arpBright ? &kSaw : &kPluck, {0.004f, 9.0f, 0.0f, 0.12f}, style.arpBright ? 900.0f : 0.0f,
                         style.arpBright ? 3500.0f : 0.0f, 0.0f};
    const Voice bassVoice{&kSaw, {0.006f, 5.0f, 0.35f, 0.06f}, 140.0f, 900.0f, 0.003f};
    const Voice subVoice{&kWarm, {0.2f, 0.5f, 0.8f, 0.6f}, 0.0f, 0.0f, 0.0f};

    double beat = 60.0 / style.bpm;
    double bar = 4.0 * beat;
    constexpr std::array<int, 8> kArpOrder = {0, 1, 2, 3, 4, 3, 2, 1};
    for (int b = 0; b < kBars; ++b) {
        const Chord& chord = style.bars[static_cast<size_t>(b)];
        double barStart = offset + bar * b;
        for (int tone : chord.pad) addNote(pads, padVoice, noteHz(tone), barStart, static_cast<float>(bar), style.padGain);

        double arpStep = bar / style.arpSteps;
        for (int s = 0; s < style.arpSteps; ++s) {
            int k = kArpOrder[static_cast<size_t>((s + b) % 8)];
            int tone = k < 3 ? chord.pad[static_cast<size_t>(k)] : chord.pad[static_cast<size_t>(k - 3)] + 12;
            float accent = s % 4 == 0 ? 1.0f : 0.75f;
            addNote(arp, arpVoice, noteHz(tone + 12), barStart + arpStep * s, static_cast<float>(arpStep * 0.6),
                    style.arpGain * accent);
        }

        if (style.bassSteps <= 1) {
            addNote(bass, subVoice, noteHz(chord.bass - 12), barStart, static_cast<float>(bar), style.bassGain);
        } else {
            double step = bar / style.bassSteps;
            for (int s = 0; s < style.bassSteps; ++s) {
                bool octave = style.bassSteps == 16 ? s % 2 == 1 : (s % 8 == 2 || s % 8 == 5);
                addNote(bass, bassVoice, noteHz(chord.bass - 12 + (octave ? 12 : 0)), barStart + step * s,
                        static_cast<float>(step * 0.7), style.bassGain * (s % 4 == 0 ? 1.0f : 0.8f));
            }
        }

        for (int q = 0; q < 4; ++q) {
            double at = barStart + beat * q;
            if (style.kickGain > 0.0f && (style.fourOnFloor || q % 2 == 0)) addKick(drums, at, style.kickGain);
            if (style.snareGain > 0.0f && q % 2 == 1) addSnare(drums, at, style.snareGain, rng);
        }
        if (style.hatSteps > 0) {
            double step = bar / style.hatSteps;
            for (int s = 0; s < style.hatSteps; ++s) {
                if (style.hatSteps == 8 && s % 2 == 0) continue; // off-beats only
                addHat(drums, barStart + step * s, style.hatGain * (s % 2 == 0 ? 0.7f : 1.0f), rng);
            }
        }
    }
}

} // namespace

const char* trackName(Track track) {
    switch (track) {
        case Track::Title: return "music_title";
        case Track::Cliff: return "music_cliff";
        case Track::Freefall: return "music_freefall";
    }
    return "music";
}

float trackBpm(Track track) { return styleFor(track).bpm; }

std::vector<float> synthesizeTrack(Track track) {
    Style style = styleFor(track);
    double loopSeconds = kBars * 4.0 * 60.0 / style.bpm;
    auto loopFrames = static_cast<size_t>(std::llround(loopSeconds * kRate));
    // Render the pattern twice and keep the second pass, so tails from the end
    // (pad releases, echoes) are already ringing at the start of the loop.
    std::vector<float> pads(2 * loopFrames, 0.0f);
    std::vector<float> arp(pads.size(), 0.0f);
    std::vector<float> bass(pads.size(), 0.0f);
    std::vector<float> drums(pads.size(), 0.0f);
    std::mt19937 rng(static_cast<uint32_t>(track) * 977u + 5u);
    for (int pass = 0; pass < 2; ++pass) {
        renderPass(style, loopSeconds * pass, pads, arp, bass, drums, rng);
    }
    double beat = 60.0 / style.bpm;
    addEcho(arp, static_cast<float>(beat * 0.75), 0.4f, style.echoMix);
    addEcho(pads, static_cast<float>(beat * 1.5), 0.3f, style.echoMix * 0.6f);

    std::vector<float> out(loopFrames);
    float peak = 0.0f;
    for (size_t i = 0; i < loopFrames; ++i) {
        size_t j = loopFrames + i;
        out[i] = pads[j] + arp[j] + bass[j] + drums[j];
        peak = std::max(peak, std::abs(out[i]));
    }
    float gain = 0.85f / std::max(peak, 1e-6f);
    for (float& s : out) s *= gain;
    return out;
}

bool MusicPlayer::load(core::Audio& audio, const std::string& directory) {
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    size_t loaded = 0;
    for (size_t t = 0; t < kTrackCount; ++t) {
        auto track = static_cast<Track>(t);
        std::string path = directory + "/" + trackName(track) + ".wav";
        handles_[t] = core::kInvalidSoundHandle;
        if (!core::encodeFloatMonoToWavFile(path, synthesizeTrack(track), kSfxSampleRate)) {
            std::fprintf(stderr, "brokenbones: could not write %s\n", path.c_str());
            continue;
        }
        handles_[t] = audio.loadSound(path);
        if (handles_[t] == core::kInvalidSoundHandle) continue;
        audio.setSoundSpatialized(handles_[t], false);
        audio.setSoundLooping(handles_[t], true);
        ++loaded;
    }
    if (loaded == 0) return false;
    audio_ = &audio;
    return true;
}

void MusicPlayer::update(float dt) {
    if (audio_ == nullptr) return;
    float step = std::max(dt, 0.0f) / kFadeSeconds;
    smoothedDuck_ += (duck_ - smoothedDuck_) * std::min(1.0f, std::max(dt, 0.0f) * 4.0f);
    for (size_t t = 0; t < kTrackCount; ++t) {
        if (handles_[t] == core::kInvalidSoundHandle) continue;
        bool wanted = target_.has_value() && static_cast<size_t>(*target_) == t;
        levels_[t] = std::clamp(levels_[t] + (wanted ? step : -step), 0.0f, 1.0f);
        if (levels_[t] > 0.0f && !playing_[t]) {
            audio_->playOneShot(handles_[t]);
            playing_[t] = true;
        } else if (levels_[t] <= 0.0f && playing_[t]) {
            audio_->stopSound(handles_[t]);
            playing_[t] = false;
        }
        if (playing_[t]) audio_->setSoundVolume(handles_[t], levels_[t] * levels_[t] * volume_ * smoothedDuck_ * 0.55f);
    }
}

void MusicPlayer::stop() {
    target_.reset();
    for (size_t t = 0; t < kTrackCount; ++t) {
        if (playing_[t] && audio_ != nullptr) audio_->stopSound(handles_[t]);
        playing_[t] = false;
        levels_[t] = 0.0f;
    }
}

} // namespace engine::brokenbones
