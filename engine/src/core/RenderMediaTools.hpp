#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace engine::core::rendermedia {

// Ring allocator for mapped upload memory (233): aligned sub-allocations that wrap around.
class UploadRing {
public:
    explicit UploadRing(uint64_t size) : size_(size) {}
    // Returns the offset, or -1 when it does not fit before the oldest data still in use.
    int64_t allocate(uint64_t bytes, uint64_t alignment) {
        if (bytes == 0 || bytes > size_) return -1;
        uint64_t start = alignUp(head_, alignment);
        if (start + bytes > size_) start = 0;
        if (used_ + bytes + (start < head_ ? size_ - head_ : start - head_) > size_) return -1;
        used_ += bytes + (start < head_ ? size_ - head_ : start - head_);
        head_ = start + bytes;
        return static_cast<int64_t>(start);
    }
    void releaseAll() {
        used_ = 0;
        head_ = 0;
    }
    [[nodiscard]] uint64_t used() const { return used_; }

private:
    static uint64_t alignUp(uint64_t v, uint64_t a) { return a <= 1 ? v : (v + a - 1) / a * a; }
    uint64_t size_, head_ = 0, used_ = 0;
};

// Upload queue with a per-frame byte budget (234, 235).
class UploadQueue {
public:
    void push(const std::string& name, uint64_t bytes) { queue_.push_back({name, bytes}); }
    std::vector<std::string> takeForFrame(uint64_t budgetBytes) {
        std::vector<std::string> done;
        uint64_t spent = 0;
        while (!queue_.empty()) {
            const auto& front = queue_.front();
            if (spent > 0 && spent + front.second > budgetBytes) break;
            spent += front.second;
            done.push_back(front.first);
            queue_.pop_front();
        }
        return done;
    }
    [[nodiscard]] std::size_t pending() const { return queue_.size(); }

private:
    std::deque<std::pair<std::string, uint64_t>> queue_;
};

// Descriptor pool diagnostics (237).
inline std::string descriptorStatus(uint32_t used, uint32_t capacity) {
    if (capacity == 0) return "no pool";
    const double p = 100.0 * used / capacity;
    return p >= 95 ? "full" : p >= 80 ? "warning: nearly full" : "ok";
}

// Command queue profile (238).
class CommandProfile {
public:
    void add(const std::string& name, double ms) {
        auto& e = totals_[name];
        e.first += ms;
        ++e.second;
    }
    [[nodiscard]] std::vector<std::pair<std::string, double>> slowest(std::size_t n) const {
        std::vector<std::pair<std::string, double>> out;
        for (const auto& [k, v] : totals_) out.push_back({k, v.first});
        std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
        if (out.size() > n) out.resize(n);
        return out;
    }
    [[nodiscard]] int countOf(const std::string& name) const { return totals_.count(name) ? totals_.at(name).second : 0; }

private:
    std::map<std::string, std::pair<double, int>> totals_;
};

// Pipeline cache file check (239): header is [magic u32][driver version u32][vendor u32].
struct PipelineCacheHeader {
    uint32_t magic = 0x4B504331, driverVersion = 0, vendor = 0;
};

inline bool pipelineCacheUsable(const PipelineCacheHeader& saved, uint32_t driverVersion, uint32_t vendor) {
    return saved.magic == 0x4B504331 && saved.driverVersion == driverVersion && saved.vendor == vendor;
}

// Debug markers (241).
inline std::string markerName(const std::string& pass, int index) { return index < 0 ? pass : pass + " #" + std::to_string(index); }

// Swapchain (243): which Vulkan results mean "recreate".
inline bool swapchainNeedsRecreate(int vkResult) { return vkResult == 1000001003 /*SUBOPTIMAL*/ || vkResult == -1000001004 /*OUT_OF_DATE*/; }

// GPU timing (246) and per-driver feature flags (249).
inline bool gpuTimingAvailable(float timestampPeriodNs, uint32_t timestampValidBits) { return timestampPeriodNs > 0 && timestampValidBits >= 32; }

inline std::set<std::string> driverQuirks(uint32_t vendorId) {
    switch (vendorId) {
    case 0x10DE: return {};
    case 0x1002: return {"avoid_large_push_constants"};
    case 0x8086: return {"simple_shadows", "no_volumetric_fog"};
    case 0x13B5: return {"simple_shadows", "no_volumetric_fog", "low_precision"};
    default: return {"conservative"};
    }
}

// Correctness scenes (250): root mean square difference between two images (0 = same).
inline double imageRmse(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    if (a.size() != b.size() || a.empty()) return 255.0;
    double sum = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const double d = static_cast<double>(a[i]) - b[i];
        sum += d * d;
    }
    return std::sqrt(sum / a.size());
}

// Sky (263): sun elevation in degrees to a zenith colour and haze.
struct SkyColour {
    float r, g, b;
};

inline SkyColour zenithColour(float elevationDeg) {
    const float t = std::clamp((elevationDeg + 6.0f) / 36.0f, 0.0f, 1.0f);
    return {0.02f + 0.18f * t, 0.03f + 0.37f * t, 0.08f + 0.77f * t};
}

inline float cloudDensity(float coverage, float noise) { return std::clamp((noise - (1.0f - coverage)) / std::max(coverage, 0.001f), 0.0f, 1.0f); }

// Light bake invalidation (269): rebake when a static object moved, changed material emissive, or a light changed.
struct BakeChange {
    bool isStatic, movedOrChanged, isLight;
};

inline bool needsRebake(const std::vector<BakeChange>& changes) {
    return std::any_of(changes.begin(), changes.end(), [](const BakeChange& c) { return c.movedOrChanged && (c.isStatic || c.isLight); });
}

// Shadow diagnostics (271).
struct ShadowCaster {
    bool castsShadow, inLightFrustum, tiny;
};

inline std::string shadowReport(const std::vector<ShadowCaster>& casters) {
    int total = 0, wasted = 0, tiny = 0;
    for (const auto& c : casters) {
        if (!c.castsShadow) continue;
        ++total;
        if (!c.inLightFrustum) ++wasted;
        else if (c.tiny) ++tiny;
    }
    return std::to_string(total) + " shadow casters, " + std::to_string(wasted) + " outside the light, " + std::to_string(tiny) + " too small to matter";
}

// Environment profile (274).
struct EnvProfile {
    std::string name;
    float sunElevation = 45, fogDensity = 0.01f, exposure = 1;
};

inline std::string serializeEnv(const EnvProfile& p) {
    std::ostringstream o;
    o << "name=" << p.name << "\nsun=" << p.sunElevation << "\nfog=" << p.fogDensity << "\nexposure=" << p.exposure << "\n";
    return o.str();
}

inline EnvProfile parseEnv(const std::string& text) {
    EnvProfile p;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if (k == "name") p.name = v;
        else if (k == "sun") p.sunElevation = static_cast<float>(std::atof(v.c_str()));
        else if (k == "fog") p.fogDensity = static_cast<float>(std::atof(v.c_str()));
        else if (k == "exposure") p.exposure = static_cast<float>(std::atof(v.c_str()));
    }
    return p;
}

// Mesh particles (280): deterministic instance positions on a ring that rises over time.
struct Instance {
    float x, y, z, scale;
};

inline std::vector<Instance> meshParticles(int count, float radius, float t, float riseSpeed) {
    std::vector<Instance> out;
    for (int i = 0; i < count; ++i) {
        const float a = 6.2831853f * static_cast<float>(i) / std::max(count, 1);
        const float age = std::fmod(t + static_cast<float>(i) * 0.1f, 2.0f);
        out.push_back({std::cos(a) * radius, age * riseSpeed, std::sin(a) * radius, 1.0f - age / 2.0f});
    }
    return out;
}

// Decal bursts (288) and VFX timeline events (293).
struct TimedEvent {
    float time;
    std::string name;
};

inline std::vector<std::string> eventsBetween(const std::vector<TimedEvent>& events, float t0, float t1) {
    std::vector<std::string> out;
    for (const auto& e : events) {
        if (e.time >= t0 && e.time < t1) out.push_back(e.name);
    }
    return out;
}

inline std::vector<TimedEvent> burstSchedule(float start, int bursts, float interval, const std::string& name) {
    std::vector<TimedEvent> out;
    for (int i = 0; i < bursts; ++i) out.push_back({start + interval * static_cast<float>(i), name});
    return out;
}

// Thumbnails (299).
inline std::pair<int, int> thumbnailSize(int w, int h, int maxSide) {
    if (w <= 0 || h <= 0) return {maxSide, maxSide};
    const float s = std::min(1.0f, static_cast<float>(maxSide) / static_cast<float>(std::max(w, h)));
    return {std::max(1, static_cast<int>(w * s)), std::max(1, static_cast<int>(h * s))};
}

// Constraint stack (311): constraints applied in order, each with a weight.
struct ConstraintStep {
    std::function<float(float)> apply;
    float weight = 1;
};

inline float runConstraintStack(float value, const std::vector<ConstraintStep>& steps) {
    for (const auto& s : steps) value += (s.apply(value) - value) * std::clamp(s.weight, 0.0f, 1.0f);
    return value;
}

// Animation utilities (317, 319, 325, 336).
inline float lerp(float a, float b, float t) { return a + (b - a) * t; }

inline float easeInOut(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3 - 2 * t);
}

struct Key {
    float time, value;
};

inline float sampleTrack(const std::vector<Key>& keys, float t) {
    if (keys.empty()) return 0;
    if (t <= keys.front().time) return keys.front().value;
    for (std::size_t i = 1; i < keys.size(); ++i) {
        if (t <= keys[i].time) {
            const float span = keys[i].time - keys[i - 1].time;
            return span <= 0 ? keys[i].value : lerp(keys[i - 1].value, keys[i].value, (t - keys[i - 1].time) / span);
        }
    }
    return keys.back().value;
}

inline float maxPoseError(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.size() != b.size()) return 1e9f;
    float m = 0;
    for (std::size_t i = 0; i < a.size(); ++i) m = std::max(m, std::fabs(a[i] - b[i]));
    return m;
}

inline std::vector<std::string> mergeTrackNames(const std::vector<std::string>& body, const std::vector<std::string>& facial) {
    std::vector<std::string> out = body;
    for (const auto& f : facial) out.push_back("face/" + f);
    return out;
}

// Waveform peaks for display (335).
inline std::vector<float> waveformPeaks(const std::vector<float>& samples, int bins) {
    std::vector<float> out(static_cast<std::size_t>(std::max(bins, 1)), 0.0f);
    if (samples.empty()) return out;
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const std::size_t b = std::min(out.size() - 1, i * out.size() / samples.size());
        out[b] = std::max(out[b], std::fabs(samples[i]));
    }
    return out;
}

// Proxy preview (343): which level of detail to show for a heavy scene.
inline int proxyLevel(long triangles) { return triangles > 5'000'000 ? 3 : triangles > 1'000'000 ? 2 : triangles > 250'000 ? 1 : 0; }

// Video export command (349): arguments for a documented external encoder.
inline std::vector<std::string> ffmpegArgs(const std::string& framePattern, int fps, const std::string& audioFile, const std::string& output) {
    std::vector<std::string> a = {"ffmpeg", "-y", "-framerate", std::to_string(fps), "-i", framePattern};
    if (!audioFile.empty()) a.insert(a.end(), {"-i", audioFile, "-c:a", "aac"});
    a.insert(a.end(), {"-c:v", "libx264", "-pix_fmt", "yuv420p", output});
    return a;
}

// Sound library (372), conversion plan (373), streaming (383).
struct SoundEntry {
    std::string name;
    std::set<std::string> tags;
    double seconds = 0;
};

inline std::vector<std::string> findSounds(const std::vector<SoundEntry>& lib, const std::string& tag, double maxSeconds) {
    std::vector<std::string> out;
    for (const auto& s : lib) {
        if ((tag.empty() || s.tags.count(tag)) && (maxSeconds <= 0 || s.seconds <= maxSeconds)) out.push_back(s.name);
    }
    return out;
}

inline std::string convertedName(const std::string& file, const std::string& newExt) {
    const std::size_t dot = file.find_last_of('.');
    return (dot == std::string::npos ? file : file.substr(0, dot)) + "." + newExt;
}

inline bool shouldStream(double seconds, uint64_t decodedBytes) { return seconds > 30.0 || decodedBytes > (5ull << 20); }

// Capture permission (393).
class PermissionStore {
public:
    void grant(const std::string& who, const std::string& what) { granted_.insert(who + "|" + what); }
    void revoke(const std::string& who, const std::string& what) { granted_.erase(who + "|" + what); }
    [[nodiscard]] bool allowed(const std::string& who, const std::string& what) const { return granted_.count(who + "|" + what) > 0; }

private:
    std::set<std::string> granted_;
};

// Audio events (397, 398): subscribers get named events in order.
class EventBus {
public:
    using Handler = std::function<void(const std::string&)>;
    void on(const std::string& event, Handler h) { handlers_[event].push_back(std::move(h)); }
    int emit(const std::string& event, const std::string& arg) {
        int n = 0;
        auto it = handlers_.find(event);
        if (it == handlers_.end()) return 0;
        for (auto& h : it->second) {
            h(arg);
            ++n;
        }
        return n;
    }

private:
    std::map<std::string, std::vector<Handler>> handlers_;
};

// Loopback test (400): a sine wave and a zero-crossing frequency estimate.
inline std::vector<float> sineWave(float hz, int sampleRate, int count) {
    std::vector<float> out(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) out[static_cast<std::size_t>(i)] = std::sin(6.2831853f * hz * static_cast<float>(i) / static_cast<float>(sampleRate));
    return out;
}

inline float estimateFrequency(const std::vector<float>& s, int sampleRate) {
    int crossings = 0;
    for (std::size_t i = 1; i < s.size(); ++i) {
        if (s[i - 1] < 0 && s[i] >= 0) ++crossings;
    }
    return s.empty() ? 0.0f : static_cast<float>(crossings) * static_cast<float>(sampleRate) / static_cast<float>(s.size());
}

} // namespace engine::core::rendermedia
