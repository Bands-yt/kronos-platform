#pragma once

#include <chrono>
#include <string>
#include <vector>

namespace engine::core {

// Named wall-clock timers for the main systems. Times add up within a frame (the sim can tick
// several times); endFrame() turns them into a smoothed per-frame value. Main thread only.
class SystemTimers {
public:
    struct Entry {
        std::string name;
        double lastFrameMs = 0.0;
        double smoothedMs = 0.0;
    };

    static SystemTimers& instance();

    void add(const char* name, double ms);
    void endFrame();
    void reset();
    [[nodiscard]] const std::vector<Entry>& entries() const { return entries_; }

private:
    struct Pending {
        std::string name;
        double ms = 0.0;
    };
    std::vector<Entry> entries_;
    std::vector<Pending> pending_;
};

class ScopedSystemTimer {
public:
    explicit ScopedSystemTimer(const char* name) : name_(name), start_(std::chrono::steady_clock::now()) {}
    ~ScopedSystemTimer() {
        const std::chrono::duration<double, std::milli> elapsed = std::chrono::steady_clock::now() - start_;
        SystemTimers::instance().add(name_, elapsed.count());
    }
    ScopedSystemTimer(const ScopedSystemTimer&) = delete;
    ScopedSystemTimer& operator=(const ScopedSystemTimer&) = delete;

private:
    const char* name_;
    std::chrono::steady_clock::time_point start_;
};

} // namespace engine::core
