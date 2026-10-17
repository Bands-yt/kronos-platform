#pragma once

#include <algorithm>
#include <cmath>
#include <deque>
#include <map>
#include <string>
#include <vector>

namespace engine::core::perf {

// Hitch detector (868, 869): a frame is a hitch when it is much slower than the recent median.
class HitchDetector {
public:
    explicit HitchDetector(std::size_t window = 120) : window_(window) {}
    // Returns true when this frame is a hitch. `cause` is remembered with the count ("asset load", "shader compile").
    bool onFrame(double frameMs, const std::string& cause = {}) {
        bool hitch = false;
        if (frames_.size() >= 20) {
            std::vector<double> sorted(frames_.begin(), frames_.end());
            std::nth_element(sorted.begin(), sorted.begin() + sorted.size() / 2, sorted.end());
            const double median = sorted[sorted.size() / 2];
            hitch = frameMs > std::max(median * 2.0, median + 8.0);
        }
        if (hitch) ++causes_[cause.empty() ? "unknown" : cause];
        frames_.push_back(frameMs);
        if (frames_.size() > window_) frames_.pop_front();
        return hitch;
    }
    [[nodiscard]] const std::map<std::string, int>& causes() const { return causes_; }

private:
    std::size_t window_;
    std::deque<double> frames_;
    std::map<std::string, int> causes_;
};

// Frame pacing (873): missed deadlines and jitter.
struct PacingReport {
    int frames = 0, missed = 0;
    double averageMs = 0, jitterMs = 0; // jitter = standard deviation
};

inline PacingReport pacing(const std::vector<double>& frameMs, double targetMs) {
    PacingReport r;
    r.frames = static_cast<int>(frameMs.size());
    if (frameMs.empty()) return r;
    double sum = 0;
    for (double f : frameMs) {
        sum += f;
        if (f > targetMs * 1.1) ++r.missed;
    }
    r.averageMs = sum / frameMs.size();
    double var = 0;
    for (double f : frameMs) var += (f - r.averageMs) * (f - r.averageMs);
    r.jitterMs = std::sqrt(var / frameMs.size());
    return r;
}

// Render pass cost waterfall (860): passes sorted by cost with their share of the frame.
struct PassCost {
    std::string name;
    double ms = 0;
    double percent = 0;
};

inline std::vector<PassCost> waterfall(const std::map<std::string, double>& passMs) {
    double total = 0;
    for (const auto& kv : passMs) total += kv.second;
    std::vector<PassCost> out;
    for (const auto& [name, ms] : passMs) out.push_back({name, ms, total > 0 ? ms / total * 100.0 : 0.0});
    std::sort(out.begin(), out.end(), [](const PassCost& a, const PassCost& b) { return a.ms > b.ms; });
    return out;
}

// Bandwidth over the last N seconds (858).
class BandwidthMeter {
public:
    explicit BandwidthMeter(double windowSeconds = 5.0) : window_(windowSeconds) {}
    void add(double now, std::size_t bytes) {
        samples_.push_back({now, bytes});
        while (!samples_.empty() && now - samples_.front().first > window_) samples_.pop_front();
    }
    [[nodiscard]] double bytesPerSecond() const {
        std::size_t total = 0;
        for (const auto& s : samples_) total += s.second;
        return static_cast<double>(total) / window_;
    }

private:
    double window_;
    std::deque<std::pair<double, std::size_t>> samples_;
};

// Physics metrics line (855).
inline std::string physicsMetricsLine(int bodies, int active, int contacts, int broadphasePairs) {
    return std::to_string(bodies) + " bodies (" + std::to_string(active) + " awake), " + std::to_string(contacts) + " contacts, " +
           std::to_string(broadphasePairs) + " broadphase pairs";
}

// Batching efficiency (872): objects per draw call; low means batching or instancing is not helping.
struct BatchReport {
    double objectsPerDraw = 0;
    std::string verdict;
};

inline BatchReport batchEfficiency(int objects, int drawCalls) {
    BatchReport r;
    r.objectsPerDraw = drawCalls > 0 ? static_cast<double>(objects) / drawCalls : 0.0;
    r.verdict = r.objectsPerDraw >= 8 ? "good" : r.objectsPerDraw >= 2 ? "could batch more" : "almost one draw call per object";
    return r;
}

// Performance budgets for CI (875): a measured value above its budget is a failure.
struct Budget {
    std::string name;
    double max = 0;
};

inline std::vector<std::string> checkBudgets(const std::vector<Budget>& budgets, const std::map<std::string, double>& measured) {
    std::vector<std::string> failures;
    for (const auto& b : budgets) {
        auto it = measured.find(b.name);
        if (it == measured.end()) failures.push_back(b.name + ": not measured");
        else if (it->second > b.max) failures.push_back(b.name + ": " + std::to_string(it->second) + " is over the budget of " + std::to_string(b.max));
    }
    return failures;
}

// Large scene benchmark (862): positions of n objects on a square grid.
struct GridPoint {
    float x, z;
};

inline std::vector<GridPoint> gridScene(int n, float spacing = 3.0f) {
    std::vector<GridPoint> out;
    const int side = std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<double>(n)))));
    for (int i = 0; i < n; ++i) out.push_back({static_cast<float>(i % side) * spacing, static_cast<float>(i / side) * spacing});
    return out;
}

} // namespace engine::core::perf
