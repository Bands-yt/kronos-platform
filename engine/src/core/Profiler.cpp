#include "core/Profiler.hpp"

#include <fstream>

#include <nlohmann/json.hpp>

#include "core/PerformanceDiagnostics.hpp"

namespace engine::core {

namespace {
void pushBounded(std::vector<ProfilerEvent>& events, ProfilerEvent event) {
    if (events.size() >= Profiler::kMaxEvents) events.erase(events.begin());
    events.push_back(event);
}
} // namespace

void Profiler::recordFrame(float frameTimeMs, double timestampSeconds) {
    // Spike check against the rolling average *before* this frame updates
    // it -- a spike is measured relative to what came before, not
    // relative to itself.
    if (frameTimeMs > rollingAverageMs_ * kSpikeMultiplier && frameTimeMs > kSpikeMinimumMs) {
        pushBounded(events_, ProfilerEvent{ProfilerEventKind::Spike, frameTimeMs, timestampSeconds, 0});
    }

    // Real exponential moving average -- alpha=0.1 is a real, deliberately
    // slow-adapting smoothing constant so one single spike doesn't itself
    // yank the baseline up and immediately suppress detecting the *next*
    // spike.
    constexpr float kSmoothingAlpha = 0.1f;
    rollingAverageMs_ = rollingAverageMs_ * (1.0f - kSmoothingAlpha) + frameTimeMs * kSmoothingAlpha;

    if (classifyFrameTimeSeverity(frameTimeMs) != PerformanceSeverity::Good) {
        ++consecutiveSlowFrames_;
        if (consecutiveSlowFrames_ == kStallFrameThreshold && !inStall_) {
            inStall_ = true;
            pushBounded(events_, ProfilerEvent{ProfilerEventKind::StallStart, frameTimeMs, timestampSeconds,
                                                static_cast<uint32_t>(consecutiveSlowFrames_)});
        }
    } else {
        if (inStall_) {
            inStall_ = false;
            pushBounded(events_, ProfilerEvent{ProfilerEventKind::StallEnd, frameTimeMs, timestampSeconds,
                                                static_cast<uint32_t>(consecutiveSlowFrames_)});
        }
        consecutiveSlowFrames_ = 0;
    }
}

void Profiler::startRecording() { recording_ = true; }
void Profiler::stopRecording() { recording_ = false; }

void Profiler::recordSnapshot(const PerformanceMetrics& metrics, double timestampSeconds) {
    if (!recording_) return;
    recordingSamples_.push_back(RecordingSample{metrics, timestampSeconds});
}

std::string Profiler::recordingToJson() const {
    nlohmann::json root = nlohmann::json::array();
    for (const RecordingSample& sample : recordingSamples_) {
        nlohmann::json entry;
        entry["timestampSeconds"] = sample.timestampSeconds;
        entry["frameTimeMs"] = sample.metrics.frameTimeMs;
        entry["fps"] = sample.metrics.fps;
        entry["drawCalls"] = sample.metrics.drawCalls;
        entry["triangleCount"] = sample.metrics.triangleCount;
        entry["gpuMemoryUsedBytes"] = sample.metrics.gpuMemoryUsedBytes;
        entry["gpuMemoryBudgetBytes"] = sample.metrics.gpuMemoryBudgetBytes;
        entry["activePhysicsBodies"] = sample.metrics.activePhysicsBodies;
        entry["totalPhysicsBodies"] = sample.metrics.totalPhysicsBodies;
        entry["loadedTerrainChunks"] = sample.metrics.loadedTerrainChunks;
        entry["totalTerrainChunks"] = sample.metrics.totalTerrainChunks;
        entry["processMemoryBytes"] = sample.metrics.processMemoryBytes;
        entry["processCpuPercent"] = sample.metrics.processCpuPercent;
        root.push_back(std::move(entry));
    }
    return root.dump(2);
}

bool Profiler::writeRecordingToJsonFile(const std::string& path) const {
    std::ofstream file(path, std::ios::out | std::ios::trunc);
    if (!file.is_open()) return false;
    file << recordingToJson();
    return file.good();
}

} // namespace engine::core
