#pragma once

#include <cstdint>
#include <deque>

#include <glm/glm.hpp>

namespace engine::anticheat {

using PlayerId = uint32_t;

// One sample of what a player's client claimed to do, timestamped
// server-side on receipt -- not on the client's clock, since a client lying
// about timing is exactly the kind of thing this exists to catch. Feeds
// docs/ARCHITECTURE.md §11's behavioral ML: "a lightweight, secondary-layer
// model over per-session input/action telemetry (superhuman reaction time,
// impossible aim-snap patterns, movement inconsistent with physics even
// accounting for prediction)".
struct TelemetrySample {
    double serverTimestamp = 0.0;
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};
    float yaw = 0.0f;
    float pitch = 0.0f;
    bool firedAction = false;
};

// A fixed-length rolling window of samples for one player, cheap enough to
// keep for every connected player without a dedicated telemetry service --
// §11 is explicit this is a *secondary* layer behind server authority
// (Principle 3), not the primary defense, so it doesn't need to retain
// more than a short window to be useful as an anomaly-detection input.
//
// No detection logic lives here -- this is the data structure a real
// behavioral model (per docs/ARCHITECTURE.md §3's "Rust service + ONNX
// Runtime anomaly model" stack entry) would read from. Scoring belongs in
// that model/service, not hardcoded as a handful of magic thresholds here.
class BehavioralTelemetry {
public:
    explicit BehavioralTelemetry(size_t maxSamples = 600) : maxSamples_(maxSamples) {} // ~10s at 60Hz

    void record(const TelemetrySample& sample) {
        samples_.push_back(sample);
        while (samples_.size() > maxSamples_) samples_.pop_front();
    }

    [[nodiscard]] const std::deque<TelemetrySample>& samples() const { return samples_; }
    [[nodiscard]] bool empty() const { return samples_.empty(); }

private:
    std::deque<TelemetrySample> samples_;
    size_t maxSamples_;
};

} // namespace engine::anticheat
