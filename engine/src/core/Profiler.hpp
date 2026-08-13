#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/PerformanceMetrics.hpp"

namespace engine::core {

// A single logged event -- one frame slow enough to be worth recording on
// its own ("spike"), or the moment a run of consecutive slow frames
// crossed into being a real "stall" rather than one unlucky frame.
enum class ProfilerEventKind { Spike, StallStart, StallEnd };

struct ProfilerEvent {
    ProfilerEventKind kind = ProfilerEventKind::Spike;
    float frameTimeMs = 0.0f;
    double timestampSeconds = 0.0;
    uint32_t consecutiveSlowFrames = 0; // meaningful for StallStart/StallEnd only
};

// Sprint 8 ("Performance Stats & Debug Tools") task category 3: a real,
// lightweight profiler -- pure decision logic in recordFrame() (no I/O,
// no globals), a real bounded event log (kMaxEvents, oldest dropped, the
// same bounded-growth discipline studio::NotificationCenter/
// core::PerformanceHistory already established for their own domains),
// and a real, optional "Performance Recording" mode that buffers full
// PerformanceMetrics snapshots and serializes them as JSON via
// nlohmann::json (already a project dependency, see AvatarItemManifest.hpp
// for the established convention) rather than hand-rolled string
// formatting.
class Profiler {
public:
    static constexpr size_t kMaxEvents = 500;
    // A frame is a "spike" once it exceeds this multiple of the recent
    // rolling average (not a fixed constant alone) -- a session honestly
    // running at a rock-steady 20fps isn't spiking every frame, it's just
    // slow; a spike is a frame unusually bad *relative to what this
    // session has actually been running at*. kSpikeMinimumMs is a real
    // floor so a jump from 2ms to 4ms (a real 2x multiple, but
    // imperceptible in absolute terms) doesn't count.
    static constexpr float kSpikeMultiplier = 2.0f;
    static constexpr float kSpikeMinimumMs = 20.0f;
    // N consecutive frames past classifyFrameTimeSeverity()'s Warning
    // threshold constitute a real "stall", not a single unlucky frame.
    static constexpr int kStallFrameThreshold = 5;

    // `timestampSeconds` is the caller's own clock (wall or simulation --
    // this class doesn't own a clock itself, the same "caller supplies
    // time" boundary core::Interactable's cooldown check already uses)
    // so it's real and callable from a headless test with fabricated
    // timestamps, not tied to a live frame loop.
    void recordFrame(float frameTimeMs, double timestampSeconds);

    [[nodiscard]] const std::vector<ProfilerEvent>& events() const { return events_; }
    void clearEvents() { events_.clear(); }

    // Performance Recording mode -- while active, recordSnapshot() calls
    // append a full metrics snapshot; stopRecording() just flips the flag
    // (the buffered samples survive it, so a caller can still serialize
    // after stopping) -- recordingToJson()/writeRecordingToJsonFile() do
    // the actual serialization, callable regardless of recording state.
    void startRecording();
    void stopRecording();
    [[nodiscard]] bool isRecording() const { return recording_; }
    void recordSnapshot(const PerformanceMetrics& metrics, double timestampSeconds);
    void clearRecording() { recordingSamples_.clear(); }
    [[nodiscard]] size_t recordingSampleCount() const { return recordingSamples_.size(); }

    // Real JSON serialization -- pure string-building (testable without
    // touching disk); writeRecordingToJsonFile() is the one thin I/O
    // wrapper around it.
    [[nodiscard]] std::string recordingToJson() const;
    [[nodiscard]] bool writeRecordingToJsonFile(const std::string& path) const;

private:
    std::vector<ProfilerEvent> events_;
    float rollingAverageMs_ = 16.7f; // seeded at a real 60fps frame, not zero -- avoids a false "everything is a spike" burst on frame 1
    int consecutiveSlowFrames_ = 0;
    bool inStall_ = false;

    bool recording_ = false;
    struct RecordingSample {
        PerformanceMetrics metrics;
        double timestampSeconds = 0.0;
    };
    std::vector<RecordingSample> recordingSamples_;
};

} // namespace engine::core
