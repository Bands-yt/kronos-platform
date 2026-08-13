#pragma once

#include <cstdint>

namespace engine::core {

// Sprint 8 ("Performance Stats & Debug Tools"): real, OS-reported process
// resource usage -- not estimated, not derived from this engine's own
// allocations (which would miss Vulkan driver/Luau/OS overhead entirely).
// Linux only today (this engine's one supported host platform, see
// platform/LinuxWindow.cpp) -- see ProcessStats.cpp for exactly what's
// read and why non-Linux honestly returns zeros rather than a fabricated
// number.
struct ProcessStats {
    uint64_t residentMemoryBytes = 0; // real RSS, /proc/self/status VmRSS on Linux
    float cpuPercent = 0.0f;          // real process CPU%, averaged over the interval since the previous sample()
};

// Stateful sampler -- CPU% needs two time-separated readings of
// cumulative CPU-seconds to compute a rate; a single instant reading of
// /proc/self/stat's utime+stime alone is a monotonically increasing
// total, not a percentage. Own one instance for the process's lifetime
// (Application/StudioApp both do) and call sample() once per frame or
// once per second -- calling it more often just shortens each sample's
// interval, it doesn't make the result wrong. The very first sample()
// call has no previous reading to diff against, so it honestly reports
// cpuPercent = 0.0f for that one call only.
class ProcessStatsSampler {
public:
    [[nodiscard]] ProcessStats sample();

private:
    uint64_t lastCpuTimeTicks_ = 0;
    double lastWallTimeSeconds_ = 0.0;
    bool hasPrevious_ = false;
};

} // namespace engine::core
