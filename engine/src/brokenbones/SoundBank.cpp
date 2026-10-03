#include "brokenbones/SoundBank.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <random>

namespace engine::brokenbones {

namespace {

constexpr float kPi = 3.14159265f;
constexpr float kRate = static_cast<float>(kSfxSampleRate);

struct SfxInfo {
    const char* name;
    bool loops;
    int voices;
};

constexpr std::array<SfxInfo, kSfxCount> kSfx = {{
    {"crack", false, 5},        {"skull_crack", false, 2}, {"thud", false, 5},         {"splash", false, 1},
    {"bomb", false, 2},         {"rocket_ignite", false, 1}, {"rocket_burn", true, 1}, {"rocket_blast", false, 1},
    {"float_inflate", false, 1}, {"float_pop", false, 2},   {"boing", false, 1},        {"wind", true, 1},
    {"combo_chime", false, 3},  {"coin", false, 3},         {"denied", false, 1},       {"click", false, 2},
    {"contract", false, 2},     {"fanfare", false, 1},     {"squelch", false, 3},      {"heartbeat", false, 1},
    {"ringing", false, 1},
}};

size_t frames(float seconds) { return static_cast<size_t>(seconds * kRate); }
float timeAt(size_t i) { return static_cast<float>(i) / kRate; }

class Noise {
public:
    explicit Noise(uint32_t seed) : rng_(seed) {}
    float white() { return dist_(rng_); }
    float uniform01() { return 0.5f * (dist_(rng_) + 1.0f); }

private:
    std::mt19937 rng_;
    std::uniform_real_distribution<float> dist_{-1.0f, 1.0f};
};

class OnePole {
public:
    float lowpass(float x, float cutoffHz) {
        float a = 1.0f - std::exp(-2.0f * kPi * std::max(cutoffHz, 1.0f) / kRate);
        state_ += a * (x - state_);
        return state_;
    }

private:
    float state_ = 0.0f;
};

// Chamberlin state-variable filter: band-pass output with resonance `q` (higher is narrower).
class BandPass {
public:
    float process(float x, float centreHz, float q) {
        float f = 2.0f * std::sin(kPi * std::min(centreHz, kRate * 0.2f) / kRate);
        float damping = 1.0f / std::max(q, 0.5f);
        low_ += f * band_;
        float high = x - low_ - damping * band_;
        band_ += f * high;
        return band_;
    }

private:
    float low_ = 0.0f;
    float band_ = 0.0f;
};

void normalize(std::vector<float>& samples, float peak) {
    float maxAbs = 0.0f;
    for (float s : samples) maxAbs = std::max(maxAbs, std::abs(s));
    if (maxAbs < 1e-6f) return;
    float gain = peak / maxAbs;
    for (float& s : samples) s *= gain;
}

// Short fades so nothing starts or stops with a click.
void fadeEdges(std::vector<float>& samples, float inSeconds, float outSeconds) {
    size_t in = std::min(frames(inSeconds), samples.size());
    size_t out = std::min(frames(outSeconds), samples.size());
    for (size_t i = 0; i < in; ++i) samples[i] *= static_cast<float>(i) / static_cast<float>(in);
    for (size_t i = 0; i < out; ++i) samples[samples.size() - 1 - i] *= static_cast<float>(i) / static_cast<float>(out);
}

// Crossfades the tail into the head so the buffer loops without a seam.
std::vector<float> makeLoop(std::vector<float> samples, float crossfadeSeconds) {
    size_t overlap = std::min(frames(crossfadeSeconds), samples.size() / 2);
    size_t length = samples.size() - overlap;
    for (size_t i = 0; i < overlap; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(overlap);
        samples[i] = samples[i] * t + samples[length + i] * (1.0f - t);
    }
    samples.resize(length);
    return samples;
}

float bell(float t, float freq, float decay) {
    return std::exp(-t * decay) * (std::sin(2.0f * kPi * freq * t) + 0.45f * std::sin(2.0f * kPi * freq * 2.01f * t) +
                                    0.2f * std::sin(2.0f * kPi * freq * 3.02f * t));
}

std::vector<float> crack(uint32_t variant, bool skull) {
    Noise noise(variant * 7919u + (skull ? 17u : 3u));
    float length = skull ? 0.45f : 0.22f;
    std::vector<float> out(frames(length));
    OnePole body;
    float ring = (skull ? 850.0f : 1900.0f) * (0.85f + 0.3f * noise.uniform01());
    float snaps[4];
    for (float& snap : snaps) snap = 0.004f + noise.uniform01() * (skull ? 0.05f : 0.03f);
    for (size_t i = 0; i < out.size(); ++i) {
        float t = timeAt(i);
        float n = noise.white();
        float splinter = (n - body.lowpass(n, 2500.0f)) * std::exp(-t * (skull ? 35.0f : 55.0f));
        for (float snap : snaps) {
            if (t >= snap) splinter += n * 1.6f * std::exp(-(t - snap) * 400.0f);
        }
        float woody = std::sin(2.0f * kPi * ring * t) * std::exp(-t * 70.0f) * 0.6f;
        float thump = std::sin(2.0f * kPi * (skull ? 120.0f : 190.0f) * t) * std::exp(-t * (skull ? 14.0f : 30.0f)) *
                      (skull ? 0.9f : 0.45f);
        out[i] = splinter + woody + thump;
    }
    fadeEdges(out, 0.0005f, 0.02f);
    normalize(out, 0.95f);
    return out;
}

std::vector<float> thud(uint32_t variant) {
    Noise noise(variant * 104729u + 5u);
    std::vector<float> out(frames(0.3f));
    OnePole lp;
    float base = 70.0f + 35.0f * noise.uniform01();
    float phase = 0.0f;
    for (size_t i = 0; i < out.size(); ++i) {
        float t = timeAt(i);
        float freq = base * (1.0f + 1.2f * std::exp(-t * 30.0f));
        phase += 2.0f * kPi * freq / kRate;
        float tone = std::sin(phase) * std::exp(-t * 16.0f);
        float dirt = lp.lowpass(noise.white(), 900.0f) * std::exp(-t * 35.0f) * 1.8f;
        out[i] = std::tanh(1.5f * (tone + dirt));
    }
    fadeEdges(out, 0.001f, 0.03f);
    normalize(out, 0.9f);
    return out;
}

std::vector<float> splash(uint32_t variant) {
    Noise noise(variant + 991u);
    std::vector<float> out(frames(1.5f));
    OnePole lp;
    OnePole hp;
    for (size_t i = 0; i < out.size(); ++i) {
        float t = timeAt(i);
        float n = noise.white();
        float cutoff = 7000.0f * std::exp(-t * 2.2f) + 300.0f;
        float filtered = lp.lowpass(n, cutoff);
        filtered -= hp.lowpass(filtered, 150.0f);
        float env = std::min(1.0f, t / 0.008f) * std::exp(-t * 3.2f);
        out[i] = filtered * env * 2.5f;
    }
    for (int b = 0; b < 26; ++b) {
        float start = 0.08f + noise.uniform01() * 1.0f;
        float freq = 350.0f + 900.0f * noise.uniform01();
        float gain = 0.25f * (1.0f - start / 1.2f);
        size_t first = frames(start);
        for (size_t i = first; i < std::min(out.size(), first + frames(0.06f)); ++i) {
            float t = timeAt(i - first);
            out[i] += std::sin(2.0f * kPi * freq * (1.0f + 6.0f * t) * t) * std::exp(-t * 60.0f) * gain;
        }
    }
    fadeEdges(out, 0.001f, 0.1f);
    normalize(out, 0.9f);
    return out;
}

std::vector<float> explosion(uint32_t variant, float length, float rumbleHz) {
    Noise noise(variant * 31u + 77u);
    std::vector<float> out(frames(length));
    OnePole lp;
    OnePole crackle;
    for (size_t i = 0; i < out.size(); ++i) {
        float t = timeAt(i);
        float n = noise.white();
        float cutoff = 3000.0f * std::exp(-t * 3.0f) + 120.0f;
        float roar = lp.lowpass(n, cutoff) * std::exp(-t * (3.2f / length)) * 3.0f;
        float blast = (n - crackle.lowpass(n, 1500.0f)) * std::exp(-t * 25.0f) * 1.2f;
        float rumble = std::sin(2.0f * kPi * rumbleHz * t * (1.0f - 0.25f * t / length)) * std::exp(-t * (2.5f / length));
        float pops = noise.uniform01() > 0.9994f ? noise.white() * 2.0f * std::exp(-t * 2.0f) : 0.0f;
        out[i] = std::tanh(1.8f * (roar + blast + rumble + pops));
    }
    fadeEdges(out, 0.0005f, 0.25f);
    normalize(out, 0.95f);
    return out;
}

std::vector<float> rocketIgnite(uint32_t variant) {
    Noise noise(variant + 404u);
    std::vector<float> out(frames(0.7f));
    BandPass band;
    for (size_t i = 0; i < out.size(); ++i) {
        float t = timeAt(i);
        float centre = 300.0f + 3500.0f * std::min(1.0f, t / 0.45f);
        float env = std::min(1.0f, t / 0.3f) * std::exp(-std::max(0.0f, t - 0.45f) * 12.0f);
        float pop = noise.white() * std::exp(-t * 120.0f);
        out[i] = band.process(noise.white(), centre, 3.0f) * env + pop;
    }
    fadeEdges(out, 0.001f, 0.05f);
    normalize(out, 0.85f);
    return out;
}

std::vector<float> rocketBurn(uint32_t variant) {
    Noise noise(variant + 505u);
    std::vector<float> out(frames(2.2f));
    BandPass band;
    OnePole roar;
    for (size_t i = 0; i < out.size(); ++i) {
        float t = timeAt(i);
        float n = noise.white();
        float hiss = band.process(n, 900.0f + 150.0f * std::sin(2.0f * kPi * 3.0f * t), 1.5f);
        float low = roar.lowpass(n, 180.0f) * 3.0f;
        float crackle = noise.uniform01() > 0.997f ? noise.white() * 1.5f : 0.0f;
        out[i] = std::tanh(hiss * 1.5f + low + crackle);
    }
    out = makeLoop(std::move(out), 0.2f);
    normalize(out, 0.8f);
    return out;
}

std::vector<float> floatInflate(uint32_t variant) {
    Noise noise(variant + 606u);
    std::vector<float> out(frames(0.5f));
    BandPass band;
    float phase = 0.0f;
    for (size_t i = 0; i < out.size(); ++i) {
        float t = timeAt(i);
        float env = std::min(1.0f, t / 0.05f) * std::exp(-t * 5.0f);
        float air = band.process(noise.white(), 500.0f + 2500.0f * t, 2.0f);
        float freq = 520.0f + 500.0f * t + 25.0f * std::sin(2.0f * kPi * 18.0f * t);
        phase += 2.0f * kPi * freq / kRate;
        out[i] = (air * 1.4f + std::sin(phase) * 0.35f) * env;
    }
    fadeEdges(out, 0.002f, 0.05f);
    normalize(out, 0.8f);
    return out;
}

std::vector<float> floatPop(uint32_t variant) {
    Noise noise(variant + 707u);
    std::vector<float> out(frames(0.18f));
    OnePole lp;
    for (size_t i = 0; i < out.size(); ++i) {
        float t = timeAt(i);
        float n = noise.white();
        out[i] = (n - lp.lowpass(n, 600.0f)) * std::exp(-t * 70.0f) + std::sin(2.0f * kPi * 260.0f * t) * std::exp(-t * 40.0f) * 0.4f;
    }
    fadeEdges(out, 0.0003f, 0.02f);
    normalize(out, 0.9f);
    return out;
}

std::vector<float> boing(uint32_t) {
    std::vector<float> out(frames(0.7f));
    float phase = 0.0f;
    for (size_t i = 0; i < out.size(); ++i) {
        float t = timeAt(i);
        float wobble = std::exp(-t * 5.0f) * std::sin(2.0f * kPi * 11.0f * t);
        float freq = 140.0f + 200.0f * std::exp(-t * 3.0f) * (1.0f + 0.5f * wobble);
        phase += 2.0f * kPi * freq / kRate;
        out[i] = (std::sin(phase) + 0.3f * std::sin(2.0f * phase)) * std::exp(-t * 4.5f);
    }
    fadeEdges(out, 0.002f, 0.05f);
    normalize(out, 0.85f);
    return out;
}

std::vector<float> wind(uint32_t variant) {
    Noise noise(variant + 808u);
    std::vector<float> out(frames(4.4f));
    OnePole lp;
    OnePole hp;
    BandPass whistle;
    for (size_t i = 0; i < out.size(); ++i) {
        float t = timeAt(i);
        float n = noise.white();
        float gust = 0.6f + 0.4f * std::sin(2.0f * kPi * 0.45f * t) * std::sin(2.0f * kPi * 0.17f * t + 1.0f);
        float body = lp.lowpass(n, 700.0f + 500.0f * gust);
        body -= hp.lowpass(body, 60.0f);
        float whine = whistle.process(n, 1300.0f + 400.0f * gust, 12.0f) * 0.25f;
        out[i] = (body * 3.0f + whine) * gust;
    }
    out = makeLoop(std::move(out), 0.4f);
    normalize(out, 0.8f);
    return out;
}

std::vector<float> notes(const std::vector<std::pair<float, float>>& startAndFreq, float length, float decay) {
    std::vector<float> out(frames(length));
    for (const auto& [start, freq] : startAndFreq) {
        size_t first = frames(start);
        for (size_t i = first; i < out.size(); ++i) out[i] += bell(timeAt(i - first), freq, decay);
    }
    fadeEdges(out, 0.001f, 0.05f);
    normalize(out, 0.75f);
    return out;
}

std::vector<float> coin(uint32_t) {
    std::vector<float> out(frames(0.45f));
    for (size_t i = 0; i < out.size(); ++i) {
        float t = timeAt(i);
        float freq = t < 0.07f ? 988.0f : 1319.0f;
        float local = t < 0.07f ? t : t - 0.07f;
        float square = std::sin(2.0f * kPi * freq * t) > 0.0f ? 1.0f : -1.0f;
        out[i] = square * 0.35f * std::exp(-local * (t < 0.07f ? 8.0f : 7.0f));
    }
    fadeEdges(out, 0.001f, 0.05f);
    normalize(out, 0.6f);
    return out;
}

std::vector<float> denied(uint32_t) {
    std::vector<float> out(frames(0.3f));
    for (size_t i = 0; i < out.size(); ++i) {
        float t = timeAt(i);
        bool gap = t > 0.12f && t < 0.16f;
        float freq = t < 0.14f ? 150.0f : 120.0f;
        float saw = 2.0f * (freq * t - std::floor(freq * t + 0.5f));
        out[i] = gap ? 0.0f : saw * 0.5f;
    }
    fadeEdges(out, 0.003f, 0.03f);
    normalize(out, 0.5f);
    return out;
}

std::vector<float> click(uint32_t variant) {
    Noise noise(variant + 909u);
    std::vector<float> out(frames(0.04f));
    for (size_t i = 0; i < out.size(); ++i) {
        float t = timeAt(i);
        out[i] = (std::sin(2.0f * kPi * 2200.0f * t) * 0.6f + noise.white() * 0.4f) * std::exp(-t * 180.0f);
    }
    fadeEdges(out, 0.0003f, 0.005f);
    normalize(out, 0.5f);
    return out;
}

std::vector<float> fanfare(uint32_t) {
    std::vector<float> out(frames(1.6f));
    const std::pair<float, float> steps[] = {{0.0f, 523.25f}, {0.12f, 659.25f}, {0.24f, 783.99f}, {0.36f, 1046.5f}};
    for (const auto& [start, freq] : steps) {
        size_t first = frames(start);
        bool last = freq > 1000.0f;
        for (size_t i = first; i < out.size(); ++i) {
            float t = timeAt(i - first);
            float env = last ? std::exp(-t * 2.2f) : std::exp(-t * 9.0f);
            float tone = std::sin(2.0f * kPi * freq * t) + 0.5f * std::sin(2.0f * kPi * freq * 2.0f * t) +
                         0.25f * std::sin(2.0f * kPi * freq * 3.0f * t);
            if (last) tone += 0.6f * std::sin(2.0f * kPi * freq * 0.75f * t) + 0.5f * std::sin(2.0f * kPi * freq * 0.5f * t);
            out[i] += tone * env * std::min(1.0f, t / 0.01f);
        }
    }
    fadeEdges(out, 0.001f, 0.2f);
    normalize(out, 0.7f);
    return out;
}

std::vector<float> squelch(uint32_t variant) {
    Noise noise(variant * 131u + 77u);
    std::vector<float> out(frames(0.35f));
    BandPass wet;
    OnePole body;
    for (size_t i = 0; i < out.size(); ++i) {
        float t = timeAt(i);
        float sweep = 1100.0f * std::exp(-t * 7.0f) + 220.0f;
        float n = noise.white();
        float slap = wet.process(n, sweep, 5.0f) * std::exp(-t * 11.0f);
        float thump = std::sin(2.0f * kPi * (70.0f + 60.0f * std::exp(-t * 25.0f)) * t) * std::exp(-t * 18.0f);
        out[i] = slap * 1.4f + body.lowpass(thump, 400.0f) * 0.8f;
    }
    fadeEdges(out, 0.002f, 0.05f);
    normalize(out, 0.75f);
    return out;
}

std::vector<float> heartbeat(uint32_t) {
    std::vector<float> out(frames(2.0f));
    const float beats[] = {0.0f, 0.17f, 0.95f, 1.12f};
    for (size_t b = 0; b < std::size(beats); ++b) {
        size_t first = frames(beats[b]);
        float gain = b % 2 == 0 ? 1.0f : 0.7f;
        for (size_t i = first; i < out.size() && i - first < frames(0.25f); ++i) {
            float t = timeAt(i - first);
            float hz = 48.0f + 30.0f * std::exp(-t * 30.0f);
            out[i] += std::sin(2.0f * kPi * hz * t) * std::exp(-t * 16.0f) * std::min(1.0f, t / 0.004f) * gain;
        }
    }
    fadeEdges(out, 0.001f, 0.1f);
    normalize(out, 0.9f);
    return out;
}

std::vector<float> ringing(uint32_t) {
    std::vector<float> out(frames(3.5f));
    for (size_t i = 0; i < out.size(); ++i) {
        float t = timeAt(i);
        float env = std::min(1.0f, t / 0.08f) * std::exp(-t * 0.9f);
        out[i] = (std::sin(2.0f * kPi * 3900.0f * t) + 0.6f * std::sin(2.0f * kPi * 3911.0f * t)) * env;
    }
    fadeEdges(out, 0.01f, 0.4f);
    normalize(out, 0.4f);
    return out;
}

} // namespace

const char* sfxName(Sfx sfx) { return kSfx[static_cast<size_t>(sfx)].name; }
bool sfxLoops(Sfx sfx) { return kSfx[static_cast<size_t>(sfx)].loops; }

std::vector<float> synthesizeSfx(Sfx sfx, uint32_t variant) {
    switch (sfx) {
        case Sfx::Crack: return crack(variant, false);
        case Sfx::SkullCrack: return crack(variant, true);
        case Sfx::Thud: return thud(variant);
        case Sfx::Splash: return splash(variant);
        case Sfx::Bomb: return explosion(variant, 1.6f, 55.0f);
        case Sfx::RocketIgnite: return rocketIgnite(variant);
        case Sfx::RocketBurn: return rocketBurn(variant);
        case Sfx::RocketBlast: return explosion(variant + 100u, 2.4f, 40.0f);
        case Sfx::FloatInflate: return floatInflate(variant);
        case Sfx::FloatPop: return floatPop(variant);
        case Sfx::Boing: return boing(variant);
        case Sfx::Wind: return wind(variant);
        case Sfx::ComboChime: return notes({{0.0f, 1318.5f}}, 0.4f, 9.0f);
        case Sfx::Coin: return coin(variant);
        case Sfx::Denied: return denied(variant);
        case Sfx::Click: return click(variant);
        case Sfx::Contract: return notes({{0.0f, 1046.5f}, {0.09f, 1318.5f}, {0.18f, 1568.0f}}, 0.8f, 6.0f);
        case Sfx::Fanfare: return fanfare(variant);
        case Sfx::Squelch: return squelch(variant);
        case Sfx::Heartbeat: return heartbeat(variant);
        case Sfx::Ringing: return ringing(variant);
    }
    return {};
}

bool SoundBank::load(core::Audio& audio, const std::string& directory) {
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    size_t loadedVoices = 0;
    for (size_t s = 0; s < kSfxCount; ++s) {
        auto sfx = static_cast<Sfx>(s);
        for (int v = 0; v < kSfx[s].voices; ++v) {
            std::string path = directory + "/" + kSfx[s].name + "_" + std::to_string(v) + ".wav";
            if (!core::encodeFloatMonoToWavFile(path, synthesizeSfx(sfx, static_cast<uint32_t>(v)), kSfxSampleRate)) {
                std::fprintf(stderr, "brokenbones: could not write %s\n", path.c_str());
                continue;
            }
            core::SoundHandle handle = audio.loadSound(path);
            if (handle == core::kInvalidSoundHandle) continue;
            audio.setSoundSpatialized(handle, false);
            audio.setSoundLooping(handle, kSfx[s].loops);
            voices_[s].push_back(handle);
            ++loadedVoices;
        }
    }
    if (loadedVoices == 0) return false;
    audio_ = &audio;
    std::fprintf(stdout, "brokenbones: %zu sound voices ready in %s\n", loadedVoices, directory.c_str());
    return true;
}

void SoundBank::play(Sfx sfx, float volume, float pitch) {
    const auto s = static_cast<size_t>(sfx);
    if (audio_ == nullptr || voices_[s].empty() || volume <= 0.0f) return;
    core::SoundHandle handle = voices_[s][nextVoice_[s]++ % voices_[s].size()];
    audio_->setSoundVolume(handle, std::clamp(volume, 0.0f, 1.5f));
    audio_->setSoundPitch(handle, pitch);
    audio_->playOneShot(handle);
}

void SoundBank::setLoop(Sfx sfx, float volume, float pitch) {
    const auto s = static_cast<size_t>(sfx);
    if (audio_ == nullptr || voices_[s].empty()) return;
    core::SoundHandle handle = voices_[s].front();
    audio_->setSoundVolume(handle, std::clamp(volume, 0.0f, 1.5f));
    audio_->setSoundPitch(handle, pitch);
    if (!loopPlaying_[s]) {
        audio_->playOneShot(handle);
        loopPlaying_[s] = true;
    }
}

void SoundBank::stopLoop(Sfx sfx) {
    const auto s = static_cast<size_t>(sfx);
    if (audio_ == nullptr || voices_[s].empty() || !loopPlaying_[s]) return;
    audio_->stopSound(voices_[s].front());
    loopPlaying_[s] = false;
}

void SoundBank::stopAllLoops() {
    for (size_t s = 0; s < kSfxCount; ++s) {
        if (kSfx[s].loops) stopLoop(static_cast<Sfx>(s));
    }
}

} // namespace engine::brokenbones
