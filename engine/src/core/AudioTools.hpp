#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace engine::core::audiotools {

constexpr float kPi = 3.14159265f;

inline float dbToLinear(float db) { return std::pow(10.0f, db / 20.0f); }
inline float linearToDb(float lin) { return lin <= 1e-6f ? -120.0f : 20.0f * std::log10(lin); }

// Clip and track gain multiply (355).
inline float combinedGain(float clipDb, float trackDb, bool trackMuted) { return trackMuted ? 0.0f : dbToLinear(clipDb + trackDb); }

// Loop and region playback (356): advances a position inside [regionStart, regionEnd).
struct Playback {
    double position = 0, regionStart = 0, regionEnd = 0;
    bool loop = false;
    bool finished = false;
};

inline void advance(Playback& p, double seconds) {
    p.position += seconds;
    if (p.regionEnd <= p.regionStart) return;
    if (p.position >= p.regionEnd) {
        if (p.loop) p.position = p.regionStart + std::fmod(p.position - p.regionStart, p.regionEnd - p.regionStart);
        else {
            p.position = p.regionEnd;
            p.finished = true;
        }
    }
}

// Simple energy-based onset detector (359): returns sample indexes where energy jumps by `ratio`.
inline std::vector<std::size_t> detectTransients(const std::vector<float>& s, std::size_t window = 256, float ratio = 3.0f, float floor = 0.01f) {
    std::vector<std::size_t> out;
    float prev = 0;
    for (std::size_t i = 0; i + window <= s.size(); i += window) {
        float e = 0;
        for (std::size_t k = 0; k < window; ++k) e += s[i + k] * s[i + k];
        e /= static_cast<float>(window);
        if (e > floor * floor && e > prev * ratio) out.push_back(i);
        prev = e;
    }
    return out;
}

// Biquad filter coefficients (363): peaking EQ, low shelf and high pass.
struct Biquad {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float z1 = 0, z2 = 0;
    float process(float x) {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

inline Biquad peakingEq(float sampleRate, float freq, float gainDb, float q) {
    const float A = std::pow(10.0f, gainDb / 40.0f), w = 2 * kPi * freq / sampleRate, alpha = std::sin(w) / (2 * q);
    const float a0 = 1 + alpha / A;
    Biquad f;
    f.b0 = (1 + alpha * A) / a0;
    f.b1 = -2 * std::cos(w) / a0;
    f.b2 = (1 - alpha * A) / a0;
    f.a1 = -2 * std::cos(w) / a0;
    f.a2 = (1 - alpha / A) / a0;
    return f;
}

inline Biquad highPass(float sampleRate, float freq, float q = 0.7071f) {
    const float w = 2 * kPi * freq / sampleRate, alpha = std::sin(w) / (2 * q), c = std::cos(w);
    const float a0 = 1 + alpha;
    Biquad f;
    f.b0 = (1 + c) / 2 / a0;
    f.b1 = -(1 + c) / a0;
    f.b2 = (1 + c) / 2 / a0;
    f.a1 = -2 * c / a0;
    f.a2 = (1 - alpha) / a0;
    return f;
}

// Compressor gain (dB) for an input level (dB): above the threshold, reduces by ratio:1.
inline float compressorGainDb(float inputDb, float thresholdDb, float ratio) {
    if (inputDb <= thresholdDb || ratio <= 1.0f) return 0.0f;
    return (thresholdDb + (inputDb - thresholdDb) / ratio) - inputDb;
}

// Bus routing (366): each bus has a gain and an output; resolves the total gain from a source to the master.
struct Bus {
    std::string output; // "" for master
    float gainDb = 0;
};

inline float busChainGain(const std::map<std::string, Bus>& buses, const std::string& start, int maxHops = 16) {
    float db = 0;
    std::string cur = start;
    for (int hop = 0; hop < maxHops && !cur.empty(); ++hop) {
        auto it = buses.find(cur);
        if (it == buses.end()) break;
        db += it->second.gainDb;
        cur = it->second.output;
    }
    return dbToLinear(db);
}

// Crossfade (368): equal-power gains for the outgoing and incoming clip at fraction t (0..1).
inline void crossfadeGains(float t, float& outGain, float& inGain) {
    t = std::clamp(t, 0.0f, 1.0f);
    outGain = std::cos(t * kPi / 2);
    inGain = std::sin(t * kPi / 2);
}

// Sample-rate conversion by linear interpolation (369) and a plain-words quality note.
inline std::vector<float> resampleLinear(const std::vector<float>& in, int fromRate, int toRate) {
    if (in.empty() || fromRate <= 0 || toRate <= 0) return {};
    const std::size_t n = static_cast<std::size_t>(static_cast<double>(in.size()) * toRate / fromRate);
    std::vector<float> out(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double pos = static_cast<double>(i) * fromRate / toRate;
        const std::size_t a = static_cast<std::size_t>(pos), b = std::min(a + 1, in.size() - 1);
        const float f = static_cast<float>(pos - static_cast<double>(a));
        out[i] = in[a] + (in[b] - in[a]) * f;
    }
    return out;
}

inline std::string resampleQualityNote(int fromRate, int toRate) {
    if (fromRate == toRate) return "Same rate: no conversion.";
    if (toRate < fromRate) return "Going down loses everything above " + std::to_string(toRate / 2) + " Hz; filter first to avoid aliasing.";
    return "Going up adds no detail; linear interpolation can sound dull above 10 kHz.";
}

// Mixdown (375): sums tracks (already at the same rate) with gains, then reports whether it clipped.
inline std::vector<float> mixdown(const std::vector<std::vector<float>>& tracks, const std::vector<float>& gains, bool& clipped) {
    std::size_t len = 0;
    for (const auto& t : tracks) len = std::max(len, t.size());
    std::vector<float> out(len, 0.0f);
    for (std::size_t k = 0; k < tracks.size(); ++k) {
        const float g = k < gains.size() ? gains[k] : 1.0f;
        for (std::size_t i = 0; i < tracks[k].size(); ++i) out[i] += tracks[k][i] * g;
    }
    clipped = false;
    for (float v : out) clipped = clipped || std::fabs(v) > 1.0f;
    return out;
}

// Audio zones (381): the reverb preset of the first zone box that contains the listener.
struct AudioZone {
    float min[3], max[3];
    std::string reverb;
};

inline std::string zoneReverbAt(const std::vector<AudioZone>& zones, const float p[3]) {
    for (const auto& z : zones) {
        if (p[0] >= z.min[0] && p[0] <= z.max[0] && p[1] >= z.min[1] && p[1] <= z.max[1] && p[2] >= z.min[2] && p[2] <= z.max[2]) return z.reverb;
    }
    return {};
}

// Ducking (385): follows a sidechain level and lowers the music by up to `maxDuckDb`, with attack/release smoothing.
class Ducker {
public:
    Ducker(float maxDuckDb = -12.0f, float attackSec = 0.05f, float releaseSec = 0.4f) : maxDuck_(maxDuckDb), attack_(attackSec), release_(releaseSec) {}
    float update(bool sidechainActive, float dt) {
        const float target = sidechainActive ? maxDuck_ : 0.0f;
        const float rate = target < db_ ? 1.0f / std::max(attack_, 1e-3f) : 1.0f / std::max(release_, 1e-3f);
        const float step = std::fabs(maxDuck_) * rate * dt;
        if (db_ < target) db_ = std::min(db_ + step, target);
        else db_ = std::max(db_ - step, target);
        return db_;
    }

private:
    float maxDuck_, attack_, release_, db_ = 0;
};

// Voices (390, 391): priority-based allocation. A new sound steals the quietest, lowest-priority voice if it outranks it.
struct Voice {
    int id = 0;
    int priority = 0; // higher = more important
    float loudness = 0;
};

class VoiceAllocator {
public:
    explicit VoiceAllocator(std::size_t maxVoices) : max_(maxVoices) {}
    // Returns the id of a voice that was stopped to make room, 0 if none was needed, -1 if the new sound was refused.
    int request(const Voice& v) {
        if (voices_.size() < max_) {
            voices_.push_back(v);
            return 0;
        }
        auto victim = std::min_element(voices_.begin(), voices_.end(), [](const Voice& a, const Voice& b) {
            return a.priority != b.priority ? a.priority < b.priority : a.loudness < b.loudness;
        });
        if (victim->priority > v.priority || (victim->priority == v.priority && victim->loudness >= v.loudness)) return -1;
        const int id = victim->id;
        *victim = v;
        return id;
    }
    void stop(int id) { voices_.erase(std::remove_if(voices_.begin(), voices_.end(), [&](const Voice& v) { return v.id == id; }), voices_.end()); }
    [[nodiscard]] std::size_t size() const { return voices_.size(); }

private:
    std::size_t max_;
    std::vector<Voice> voices_;
};

// Master limiter (394): soft-clips above the ceiling so the output never passes it.
inline float softLimit(float x, float ceiling = 0.98f) {
    const float a = std::fabs(x);
    if (a <= ceiling * 0.8f) return x;
    const float knee = ceiling * 0.8f, over = a - knee, room = ceiling - knee;
    const float limited = knee + room * std::tanh(over / room);
    return x < 0 ? -limited : limited;
}

// Peak meter with hold and a clipping warning (395).
class PeakMeter {
public:
    void push(float sample, double nowSeconds) {
        const float a = std::fabs(sample);
        if (a >= peak_ || nowSeconds - peakTime_ > holdSeconds) {
            peak_ = a;
            peakTime_ = nowSeconds;
        }
        if (a > 1.0f) clipped_ = true;
    }
    [[nodiscard]] float peak() const { return peak_; }
    [[nodiscard]] bool clipped() const { return clipped_; }
    double holdSeconds = 1.5;

private:
    float peak_ = 0;
    double peakTime_ = 0;
    bool clipped_ = false;
};

// Buffers (386, 389): frames per period for a latency target, and exact scheduling in frames.
inline int periodFramesForLatency(int sampleRate, float latencyMs, int periods = 2) {
    const float frames = sampleRate * latencyMs / 1000.0f / static_cast<float>(std::max(periods, 1));
    int p = 64;
    while (p < frames && p < 4096) p *= 2;
    return p;
}

inline uint64_t secondsToFrames(double seconds, int sampleRate) { return static_cast<uint64_t>(std::llround(seconds * sampleRate)); }

} // namespace engine::core::audiotools
