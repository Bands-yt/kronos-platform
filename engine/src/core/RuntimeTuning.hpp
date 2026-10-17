#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <list>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine::core {

// --- Quality by GPU tier (0871) -------------------------------------------------------------
enum class QualityPreset { Low, Medium, High };

inline const char* qualityName(QualityPreset p) {
    switch (p) {
    case QualityPreset::Low: return "Low";
    case QualityPreset::Medium: return "Medium";
    case QualityPreset::High: return "High";
    }
    return "Medium";
}

// kind is GpuSummary::kind: "discrete", "integrated", "software" or "other".
inline QualityPreset recommendedQuality(const std::string& kind, uint64_t vramBytes) {
    if (kind == "software") return QualityPreset::Low;
    if (kind == "integrated") return QualityPreset::Low;
    if (kind == "discrete") return vramBytes >= (6ull << 30) ? QualityPreset::High : QualityPreset::Medium;
    return QualityPreset::Medium;
}

// --- Physics budget for slow CPUs (0921) ----------------------------------------------------
// Scales the number of physics steps allowed per frame and the simulation rate down when frames run long.
struct PhysicsBudget {
    int maxSubSteps = 4;
    float stepSeconds = 1.0f / 60.0f;
};

inline PhysicsBudget physicsBudgetFor(float averageFrameMs, unsigned cpuThreads) {
    PhysicsBudget b;
    if (averageFrameMs > 33.0f || cpuThreads <= 2) {
        b.maxSubSteps = 2;
        b.stepSeconds = 1.0f / 45.0f;
    } else if (averageFrameMs > 20.0f || cpuThreads <= 4) {
        b.maxSubSteps = 3;
    }
    return b;
}

// --- Reduced flashing (0888) ------------------------------------------------------------------
// Limits how often the screen may change brightness a lot: at most `maxPerSecond` large flashes.
class FlashLimiter {
public:
    explicit FlashLimiter(int maxPerSecond = 3) : maxPerSecond_(maxPerSecond) {}
    // Returns the intensity to use (0..1): full if allowed, 0 if the flash must be dropped.
    float request(double nowSeconds, float intensity) {
        while (!times_.empty() && nowSeconds - times_.front() >= 1.0) times_.erase(times_.begin());
        if (intensity < 0.25f) return intensity; // small changes are not flashes
        if (static_cast<int>(times_.size()) >= maxPerSecond_) return 0.0f;
        times_.push_back(nowSeconds);
        return intensity;
    }

private:
    int maxPerSecond_;
    std::vector<double> times_;
};

// --- Motion comfort for the camera (0889) -----------------------------------------------------
struct CameraComfort {
    bool headBob = true;
    float shakeScale = 1.0f; // 0 turns camera shake off
    float fovKick = 1.0f;    // sprint FOV widening
    float maxFovDegrees = 110.0f;
};

inline CameraComfort comfortFromReducedMotion(bool reducedMotion) {
    CameraComfort c;
    if (reducedMotion) {
        c.headBob = false;
        c.shakeScale = 0.0f;
        c.fovKick = 0.0f;
        c.maxFovDegrees = 90.0f;
    }
    return c;
}

// --- Memory-pressure eviction (914) -----------------------------------------------------------
// Least-recently-used set of asset names with a byte budget; touch() on use, add() when loaded.
class LruAssetCache {
public:
    explicit LruAssetCache(uint64_t budgetBytes) : budget_(budgetBytes) {}

    // Returns the names that must be unloaded to stay inside the budget (oldest first).
    std::vector<std::string> add(const std::string& name, uint64_t bytes) {
        remove(name);
        order_.push_front(name);
        entries_[name] = {bytes, order_.begin()};
        used_ += bytes;
        std::vector<std::string> evicted;
        while (used_ > budget_ && order_.size() > 1) {
            const std::string victim = order_.back();
            evicted.push_back(victim);
            remove(victim);
        }
        return evicted;
    }

    void touch(const std::string& name) {
        auto it = entries_.find(name);
        if (it == entries_.end()) return;
        order_.splice(order_.begin(), order_, it->second.pos);
    }

    void remove(const std::string& name) {
        auto it = entries_.find(name);
        if (it == entries_.end()) return;
        used_ -= it->second.bytes;
        order_.erase(it->second.pos);
        entries_.erase(it);
    }

    [[nodiscard]] bool has(const std::string& name) const { return entries_.count(name) > 0; }
    [[nodiscard]] uint64_t used() const { return used_; }

private:
    struct Entry {
        uint64_t bytes;
        std::list<std::string>::iterator pos;
    };
    uint64_t budget_;
    uint64_t used_ = 0;
    std::list<std::string> order_;
    std::unordered_map<std::string, Entry> entries_;
};

// --- Profiling helpers (421, 856, 399, 867, 874) ---------------------------------------------------
// Flame graph from samples: each sample is a call stack (outermost first). Builds a tree with counts.
struct FlameNode {
    std::string name;
    int samples = 0; // including children
    std::vector<FlameNode> children;
};

inline void addFlameSample(FlameNode& root, const std::vector<std::string>& stack) {
    FlameNode* node = &root;
    ++node->samples;
    for (const std::string& frame : stack) {
        auto it = std::find_if(node->children.begin(), node->children.end(),
                               [&](const FlameNode& c) { return c.name == frame; });
        if (it == node->children.end()) {
            node->children.push_back({frame, 0, {}});
            it = node->children.end() - 1;
        }
        node = &*it;
        ++node->samples;
    }
}

// "a;b;c 12" lines, the format flamegraph tools read.
inline void flameFolded(const FlameNode& node, const std::string& prefix, std::string& out) {
    const std::string here = prefix.empty() ? node.name : prefix + ";" + node.name;
    int childTotal = 0;
    for (const auto& c : node.children) childTotal += c.samples;
    if (node.samples > childTotal && !node.name.empty()) out += here + " " + std::to_string(node.samples - childTotal) + "\n";
    for (const auto& c : node.children) flameFolded(c, node.name.empty() ? std::string{} : here, out);
}

// Counts of voices per bus over time, for the audio profiler graph.
struct VoiceHistory {
    std::vector<int> counts;
    std::size_t capacity = 240;
    void push(int activeVoices) {
        counts.push_back(activeVoices);
        if (counts.size() > capacity) counts.erase(counts.begin());
    }
    [[nodiscard]] int peak() const { return counts.empty() ? 0 : *std::max_element(counts.begin(), counts.end()); }
};

// Highest value seen (GPU memory high-water mark).
struct HighWaterMark {
    uint64_t current = 0, peak = 0;
    void set(uint64_t value) {
        current = value;
        peak = std::max(peak, value);
    }
};

// Before/after comparison of benchmark medians: negative percent = faster.
inline double percentChange(double before, double after) { return before <= 0.0 ? 0.0 : (after - before) / before * 100.0; }

inline std::string benchmarkVerdict(double beforeMedian, double afterMedian, double spread) {
    const double change = afterMedian - beforeMedian;
    if (std::fabs(change) <= spread) return "no real change (within the run-to-run spread)";
    return change < 0 ? "faster" : "slower";
}

} // namespace engine::core
