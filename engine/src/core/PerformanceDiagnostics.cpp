#include "core/PerformanceDiagnostics.hpp"

#include <algorithm>
#include <numeric>

namespace engine::core {

PerformanceMetrics composePerformanceMetrics(const PerformanceMetrics& renderMetrics, uint32_t activePhysicsBodies,
                                              uint32_t totalPhysicsBodies, uint32_t loadedTerrainChunks,
                                              uint32_t totalTerrainChunks, const ProcessStats& processStats) {
    PerformanceMetrics full = renderMetrics;
    full.activePhysicsBodies = activePhysicsBodies;
    full.totalPhysicsBodies = totalPhysicsBodies;
    full.loadedTerrainChunks = loadedTerrainChunks;
    full.totalTerrainChunks = totalTerrainChunks;
    full.processMemoryBytes = processStats.residentMemoryBytes;
    full.processCpuPercent = processStats.cpuPercent;
    return full;
}

PerformanceSeverity classifyFrameTimeSeverity(float frameTimeMs) {
    if (frameTimeMs <= 16.7f) return PerformanceSeverity::Good;
    if (frameTimeMs <= 33.4f) return PerformanceSeverity::Warning;
    return PerformanceSeverity::Critical;
}

PerformanceSeverity classifyMemorySeverity(uint64_t usedBytes, uint64_t budgetBytes) {
    if (budgetBytes == 0) return PerformanceSeverity::Good;
    double fraction = static_cast<double>(usedBytes) / static_cast<double>(budgetBytes);
    if (fraction < 0.75) return PerformanceSeverity::Good;
    if (fraction < 0.90) return PerformanceSeverity::Warning;
    return PerformanceSeverity::Critical;
}

void PerformanceHistory::push(float value) {
    if (samples_.size() >= capacity_) {
        samples_.erase(samples_.begin());
    }
    samples_.push_back(value);
}

float PerformanceHistory::average() const {
    if (samples_.empty()) return 0.0f;
    float sum = std::accumulate(samples_.begin(), samples_.end(), 0.0f);
    return sum / static_cast<float>(samples_.size());
}

float PerformanceHistory::max() const {
    if (samples_.empty()) return 0.0f;
    return *std::max_element(samples_.begin(), samples_.end());
}

} // namespace engine::core
