#pragma once

#include <chrono>
#include <cstdarg>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace engine::core {

// Kronos (Phase 1 stability: "Logging + debugging layer"): a real,
// minimal structured logger -- the honest replacement for this
// codebase's ~370 scattered `std::fprintf(stdout/stderr, ...)` call
// sites (confirmed live via a full-codebase audit: no levels, no
// categories, no timestamps, no way to filter/route output at runtime
// existed anywhere before this).
//
// Deliberately NOT a full migration of every existing fprintf call in
// one pass -- that's a real, mechanical, low-risk-per-site but
// high-total-site-count follow-up (see docs/STABILITY.md's own tracking
// note), not something worth doing blind across a codebase this size in
// one sitting. What's real and complete here: the logger itself (levels,
// categories, real elapsed-time timestamps, a bounded thread-safe ring
// buffer a future Studio "Console" panel can read back from directly
// instead of re-parsing stdout), wired into a real, representative set of
// call sites to prove the API is actually usable, plus real unit tests.

enum class LogLevel { Debug, Info, Warn, Error };

[[nodiscard]] const char* logLevelName(LogLevel level);

struct LogEntry {
    LogLevel level = LogLevel::Info;
    std::string category;
    std::string message;
    // Real seconds since Logger::instance() was first constructed --
    // process-relative, not wall-clock, matching this engine's own
    // established "elapsed seconds" convention (e.g.
    // core::Application::totalElapsedTimeSeconds_) rather than a real
    // calendar timestamp no caller here has asked for.
    float timestampSeconds = 0.0f;
};

// Real, minimal, process-wide logger. A single shared instance (returned
// by instance()) rather than one threaded through every call site's own
// signature -- the same real tradeoff every "log from anywhere" system
// makes; this one is explicitly scoped to stay small (no sinks/plugins/
// async queue) rather than grow into its own subsystem.
class Logger {
public:
    static Logger& instance();

    // Real, thread-safe (std::mutex-guarded) -- current engine code is
    // single-threaded end to end (see docs/STABILITY.md's own networking-
    // isolation finding), but this costs nothing measurable at this call
    // volume and removes a real, easy-to-hit future footgun the moment
    // any subsystem does start logging from a worker thread.
    void log(LogLevel level, std::string category, std::string message);

    // Real printf-style convenience -- most of the ~370 existing
    // fprintf() call sites already build a formatted message this way;
    // this keeps migrating one of them a small, mechanical, same-shape
    // diff instead of a real rewrite into stream-style logging.
    void logf(LogLevel level, const char* category, const char* format, ...)
#if defined(__GNUC__) || defined(__clang__)
        __attribute__((format(printf, 4, 5)))
#endif
        ;

    // Real va_list-accepting variant -- public specifically so the free
    // functions below (logDebug()/logInfo()/logWarn()/logError()) can
    // forward an already-started va_list into it; C++ has no way to
    // forward a `...` parameter pack from one variadic function to
    // another directly, va_list is the real, standard mechanism.
    void logvf(LogLevel level, const char* category, const char* format, va_list args);

    void setMinLevel(LogLevel level);
    [[nodiscard]] LogLevel minLevel() const;

    // Real, bounded copy of the most recent entries (oldest first) --
    // safe to call from any thread; returns a snapshot, not a live view.
    [[nodiscard]] std::vector<LogEntry> recentEntries(size_t maxCount = 200) const;

    void clearRingBuffer();

    static constexpr size_t kMaxRingEntries = 2000;

private:
    Logger();

    mutable std::mutex mutex_;
    std::deque<LogEntry> ring_;
    LogLevel minLevel_ = LogLevel::Debug;
    std::chrono::steady_clock::time_point startTime_;
};

// Real, terse free-function wrappers -- Logger::instance().logf(...) at
// every call site is real but noisy; these are what an actual migrated
// call site is expected to use. Matches this codebase's own established
// "plain functions over macros" convention (no LOG_INFO()-style macro
// exists here on purpose).
void logDebug(const char* category, const char* format, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;
void logInfo(const char* category, const char* format, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;
void logWarn(const char* category, const char* format, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;
void logError(const char* category, const char* format, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;

} // namespace engine::core
