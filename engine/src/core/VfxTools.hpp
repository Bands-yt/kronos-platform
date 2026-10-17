#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace engine::core::vfx {

// Deterministic random numbers for replayable particle effects (283).
class SeededRng {
public:
    explicit SeededRng(uint64_t seed = 1) : s_(seed ? seed : 0x9E3779B97F4A7C15ull) {}
    uint64_t next() {
        s_ ^= s_ << 13;
        s_ ^= s_ >> 7;
        s_ ^= s_ << 17;
        return s_;
    }
    float unit() { return static_cast<float>(next() >> 40) / 16777216.0f; }
    float range(float a, float b) { return a + (b - a) * unit(); }

private:
    uint64_t s_;
};

// Fraction of normal spawn rate by distance to the camera (284).
inline float particleLodScale(float distance, float fullUntil = 20.0f, float cullAt = 100.0f) {
    if (distance <= fullUntil) return 1.0f;
    if (distance >= cullAt) return 0.0f;
    return 1.0f - (distance - fullUntil) / (cullAt - fullUntil);
}

// Flipbook (281): UV rectangle for the frame shown at `age` of a sheet with columns x rows frames.
struct UvRect {
    float u = 0, v = 0, w = 0, h = 0;
};

inline UvRect flipbookFrame(float age, float lifetime, int columns, int rows, bool loop = false) {
    const int total = std::max(columns * rows, 1);
    float t = lifetime > 0 ? age / lifetime : 0.0f;
    t = loop ? t - std::floor(t) : std::clamp(t, 0.0f, 0.999999f);
    const int frame = std::min(static_cast<int>(t * total), total - 1);
    return {static_cast<float>(frame % columns) / columns, static_cast<float>(frame / columns) / rows, 1.0f / columns, 1.0f / rows};
}

// Sub-emitters (286): when a particle dies, spawn `count` of another effect.
struct SubEmitter {
    std::string effect;
    int count = 0;
    enum class On { Death, Collision } trigger = On::Death;
};

inline int subEmitterSpawns(const std::vector<SubEmitter>& subs, SubEmitter::On event) {
    int n = 0;
    for (const auto& s : subs) n += s.trigger == event ? s.count : 0;
    return n;
}

// Beam (287): points from a to b with sideways jitter (deterministic from the rng).
struct P3 {
    float x = 0, y = 0, z = 0;
};

inline std::vector<P3> beamPoints(P3 a, P3 b, int segments, float jitter, SeededRng& rng) {
    std::vector<P3> pts;
    for (int i = 0; i <= segments; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(std::max(segments, 1));
        const bool end = i == 0 || i == segments;
        pts.push_back({a.x + (b.x - a.x) * t + (end ? 0 : rng.range(-jitter, jitter)), a.y + (b.y - a.y) * t + (end ? 0 : rng.range(-jitter, jitter)),
                       a.z + (b.z - a.z) * t + (end ? 0 : rng.range(-jitter, jitter))});
    }
    return pts;
}

// Pool (294): reuse effect instances instead of creating and freeing them.
class EffectPool {
public:
    explicit EffectPool(std::size_t capacity) : capacity_(capacity) {}
    int acquire() { // returns an instance id, or -1 when the pool is full
        if (!free_.empty()) {
            const int id = free_.back();
            free_.pop_back();
            ++active_;
            return id;
        }
        if (created_ >= capacity_) return -1;
        ++active_;
        return static_cast<int>(created_++);
    }
    void release(int id) {
        if (id < 0 || static_cast<std::size_t>(id) >= created_ || active_ == 0) return;
        free_.push_back(id);
        --active_;
    }
    [[nodiscard]] std::size_t active() const { return active_; }
    [[nodiscard]] std::size_t created() const { return created_; }

private:
    std::size_t capacity_, created_ = 0, active_ = 0;
    std::vector<int> free_;
};

// Budgets (295, 297, 298).
struct VfxProfile {
    const char* name;
    int maxParticles; // total alive at once
    int maxEmitters;
    bool softParticles;
};

inline std::vector<VfxProfile> vfxProfiles() { return {{"Desktop", 100000, 200, true}, {"Laptop", 40000, 100, true}, {"Mobile", 5000, 30, false}}; }

struct VfxUsage {
    int particles = 0, emitters = 0;
};

inline std::vector<std::string> vfxWarnings(const VfxUsage& u, const VfxProfile& p) {
    std::vector<std::string> w;
    if (u.particles > p.maxParticles) w.push_back("Particles over budget: " + std::to_string(u.particles) + " of " + std::to_string(p.maxParticles) + ".");
    else if (u.particles * 10 >= p.maxParticles * 8) w.push_back("Particles at " + std::to_string(u.particles * 100 / p.maxParticles) + "% of the budget.");
    if (u.emitters > p.maxEmitters) w.push_back("Too many emitters: " + std::to_string(u.emitters) + " of " + std::to_string(p.maxEmitters) + ".");
    return w;
}

// Script hooks (291): callbacks keyed by event name ("spawn", "death", "hit").
class ParticleEvents {
public:
    using Handler = std::function<void(int particleId)>;
    void on(const std::string& event, Handler h) { handlers_[event].push_back(std::move(h)); }
    int emit(const std::string& event, int particleId) const {
        auto it = handlers_.find(event);
        if (it == handlers_.end()) return 0;
        for (const auto& h : it->second) h(particleId);
        return static_cast<int>(it->second.size());
    }

private:
    std::map<std::string, std::vector<Handler>> handlers_;
};

// Presets (296): starting values for common effects.
struct EffectPreset {
    const char* name;
    float rate, lifetime, speed, size;
    float color[3];
    float gravity;
};

inline std::vector<EffectPreset> effectPresets() {
    return {{"Fire", 60, 0.9f, 1.5f, 0.5f, {1.0f, 0.5f, 0.1f}, -1.5f},
            {"Smoke", 20, 3.0f, 0.8f, 1.2f, {0.4f, 0.4f, 0.4f}, -0.3f},
            {"Magic sparkles", 40, 1.5f, 2.0f, 0.15f, {0.6f, 0.4f, 1.0f}, 0.0f},
            {"Sparks", 80, 0.6f, 5.0f, 0.08f, {1.0f, 0.8f, 0.3f}, 9.8f}};
}

} // namespace engine::core::vfx
