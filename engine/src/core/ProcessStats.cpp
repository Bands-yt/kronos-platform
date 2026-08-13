#include "core/ProcessStats.hpp"

#include <chrono>

#if defined(__linux__)
#include <cstdio>
#include <cstring>
#include <unistd.h>
#endif

namespace engine::core {

namespace {
double wallTimeSeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
} // namespace

#if defined(__linux__)

ProcessStats ProcessStatsSampler::sample() {
    ProcessStats stats;

    // VmRSS from /proc/self/status -- real, kernel-reported resident set
    // size (physical memory actually mapped for this process right now),
    // in kB per the /proc documentation. A plain line-scan, no parsing
    // library needed for one fixed-format pseudo-file.
    if (FILE* statusFile = std::fopen("/proc/self/status", "r")) {
        char line[256];
        while (std::fgets(line, sizeof(line), statusFile) != nullptr) {
            unsigned long long rssKb = 0;
            if (std::sscanf(line, "VmRSS: %llu kB", &rssKb) == 1) {
                stats.residentMemoryBytes = static_cast<uint64_t>(rssKb) * 1024ull;
                break;
            }
        }
        std::fclose(statusFile);
    }

    // utime+stime from /proc/self/stat -- fields 14 and 15 (1-indexed),
    // both in clock ticks (sysconf(_SC_CLK_TCK) ticks/second, not a fixed
    // 100Hz assumption -- real systems vary). The process name field (#2)
    // is parenthesized and can itself contain spaces, so this skips past
    // the closing ')' rather than naively splitting on whitespace.
    uint64_t cpuTimeTicks = 0;
    if (FILE* statFile = std::fopen("/proc/self/stat", "r")) {
        char buffer[512];
        size_t bytesRead = std::fread(buffer, 1, sizeof(buffer) - 1, statFile);
        std::fclose(statFile);
        buffer[bytesRead] = '\0';
        char* afterName = std::strrchr(buffer, ')');
        if (afterName != nullptr) {
            unsigned long long utime = 0, stime = 0;
            // Fields after ')': state(3) ppid(4) pgrp(5) session(6) tty_nr(7)
            // tpgid(8) flags(9) minflt(10) cminflt(11) majflt(12) cmajflt(13)
            // utime(14) stime(15) -- 11 fields to skip before utime.
            std::sscanf(afterName + 1, " %*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %llu %llu", &utime, &stime);
            cpuTimeTicks = static_cast<uint64_t>(utime) + static_cast<uint64_t>(stime);
        }
    }

    double nowSeconds = wallTimeSeconds();
    if (hasPrevious_) {
        long ticksPerSecond = sysconf(_SC_CLK_TCK);
        double deltaWall = nowSeconds - lastWallTimeSeconds_;
        double deltaCpuSeconds =
            ticksPerSecond > 0
                ? static_cast<double>(cpuTimeTicks - lastCpuTimeTicks_) / static_cast<double>(ticksPerSecond)
                : 0.0;
        stats.cpuPercent = deltaWall > 0.0 ? static_cast<float>((deltaCpuSeconds / deltaWall) * 100.0) : 0.0f;
    } else {
        stats.cpuPercent = 0.0f; // no previous sample to diff against -- see header comment
    }

    lastCpuTimeTicks_ = cpuTimeTicks;
    lastWallTimeSeconds_ = nowSeconds;
    hasPrevious_ = true;
    return stats;
}

#else // !__linux__

// Honest zeros, not a fabricated number -- see ProcessStats.hpp's own
// comment on why this engine only reads real OS-reported stats on its
// one currently-supported host platform. A future Windows port would
// implement this via GetProcessMemoryInfo()/GetProcessTimes(), the same
// "real, per-platform, honestly-scoped stub" pattern
// platform/WindowsWindow.cpp's own #else branch already establishes.
ProcessStats ProcessStatsSampler::sample() {
    (void)wallTimeSeconds();
    return ProcessStats{};
}

#endif

} // namespace engine::core
