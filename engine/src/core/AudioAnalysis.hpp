#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

namespace engine::core::audio_analysis {

struct Levels {
    float peak = 0.0f;
    float rms = 0.0f;
    float peakDb = -120.0f;
    float rmsDb = -120.0f;
};

inline float toDb(float linear) { return linear <= 1e-6f ? -120.0f : 20.0f * std::log10(linear); }

inline Levels measure(const std::vector<float>& samples) {
    Levels out;
    if (samples.empty()) return out;
    double sum = 0.0;
    for (float s : samples) {
        out.peak = std::max(out.peak, std::fabs(s));
        sum += static_cast<double>(s) * s;
    }
    out.rms = static_cast<float>(std::sqrt(sum / static_cast<double>(samples.size())));
    out.peakDb = toDb(out.peak);
    out.rmsDb = toDb(out.rms);
    return out;
}

// Gain that brings the peak to targetPeakDb (default -1 dB). Silence returns 1.
inline float normalizeGain(const std::vector<float>& samples, float targetPeakDb = -1.0f) {
    const Levels l = measure(samples);
    if (l.peak <= 1e-6f) return 1.0f;
    return std::pow(10.0f, targetPeakDb / 20.0f) / l.peak;
}

// Loudness gain toward a target RMS level, capped so the peak never clips.
inline float loudnessGain(const std::vector<float>& samples, float targetRmsDb = -18.0f) {
    const Levels l = measure(samples);
    if (l.rms <= 1e-6f) return 1.0f;
    const float wanted = std::pow(10.0f, targetRmsDb / 20.0f) / l.rms;
    return std::min(wanted, 1.0f / std::max(l.peak, 1e-6f));
}

struct PeakPair {
    float min = 0.0f;
    float max = 0.0f;
};

// Min/max per bucket for drawing a waveform. Always returns exactly `buckets` entries.
inline std::vector<PeakPair> waveformPeaks(const std::vector<float>& samples, std::size_t buckets) {
    std::vector<PeakPair> out(buckets);
    if (samples.empty() || buckets == 0) return out;
    for (std::size_t b = 0; b < buckets; ++b) {
        const std::size_t begin = b * samples.size() / buckets;
        std::size_t end = (b + 1) * samples.size() / buckets;
        if (end <= begin) end = std::min(begin + 1, samples.size());
        PeakPair p{samples[begin], samples[begin]};
        for (std::size_t i = begin; i < end; ++i) {
            p.min = std::min(p.min, samples[i]);
            p.max = std::max(p.max, samples[i]);
        }
        out[b] = p;
    }
    return out;
}

struct CompressionPreset {
    const char* name;
    const char* format;
    int sampleRate;
    int channels;
    int bitrateKbps; // 0 = lossless
    const char* useFor;
};

inline const std::vector<CompressionPreset>& compressionPresets() {
    static const std::vector<CompressionPreset> presets = {
        {"Voice", "ogg", 24000, 1, 48, "dialogue, announcements"},
        {"Effects", "ogg", 44100, 1, 96, "short sound effects"},
        {"Music", "ogg", 48000, 2, 160, "background music"},
        {"Music (high)", "ogg", 48000, 2, 256, "featured music"},
        {"Lossless", "wav", 48000, 2, 0, "masters and very short sounds"},
    };
    return presets;
}

inline const CompressionPreset* findPreset(const std::string& name) {
    for (const auto& p : compressionPresets()) {
        if (name == p.name) return &p;
    }
    return nullptr;
}

} // namespace engine::core::audio_analysis

namespace engine::core::audio_analysis {

struct ReverbPreset {
    const char* name;
    float roomSeconds; // decay time
    float wet;         // 0..1
    float damping;     // 0..1, higher = darker tail
};

inline const std::vector<ReverbPreset>& reverbPresets() {
    static const std::vector<ReverbPreset> list = {
        {"Small room", 0.4f, 0.15f, 0.5f}, {"Hall", 1.8f, 0.3f, 0.4f},
        {"Cave", 3.5f, 0.45f, 0.6f},       {"Bathroom", 0.9f, 0.35f, 0.2f},
        {"Outdoors", 0.2f, 0.05f, 0.7f},
    };
    return list;
}

// Chorus and delay are described by time and depth; saturation by drive (tanh soft clip).
inline float saturate(float x, float drive) {
    if (drive <= 0.0f) return x;
    return std::tanh(x * (1.0f + drive * 4.0f)) / std::tanh(1.0f + drive * 4.0f);
}

} // namespace engine::core::audio_analysis
