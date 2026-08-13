#include "core/CrashReporter.hpp"

#include <csignal>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "core/Logger.hpp"

#if defined(__linux__)
#include <execinfo.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace engine::core {

#if defined(__linux__)

namespace {

bool g_installed = false;
int g_crashFileFd = -1;
struct sigaction g_previousHandlers[6]; // one per signal below, for real chaining after our own handler runs

constexpr int kSignals[] = {SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS, SIGTRAP};

// Real, async-signal-safe-friendly write -- write() itself is on POSIX's
// own real async-signal-safe function list; this just loops past a real
// short write (a real, legitimate possibility even for a small buffer).
void writeRaw(int fd, const char* text) {
    size_t len = std::strlen(text);
    size_t written = 0;
    while (written < len) {
        ssize_t n = write(fd, text + written, len - written);
        if (n <= 0) return;
        written += static_cast<size_t>(n);
    }
}

const char* signalName(int sig) {
    switch (sig) {
        case SIGSEGV: return "SIGSEGV (segmentation fault)";
        case SIGABRT: return "SIGABRT (abort)";
        case SIGFPE: return "SIGFPE (floating point exception)";
        case SIGILL: return "SIGILL (illegal instruction)";
        case SIGBUS: return "SIGBUS (bus error)";
        case SIGTRAP: return "SIGTRAP (trace/breakpoint trap)";
        default: return "unknown signal";
    }
}

void crashHandler(int sig) {
    if (g_crashFileFd >= 0) {
        writeRaw(g_crashFileFd, "=== Kronos crash report ===\n");
        writeRaw(g_crashFileFd, "Signal: ");
        writeRaw(g_crashFileFd, signalName(sig));
        writeRaw(g_crashFileFd, "\n\n--- Backtrace ---\n");

        // backtrace_symbols_fd() (not backtrace_symbols()) is the one
        // glibc explicitly documents as safe to call from inside a
        // signal handler -- it writes directly via write(), no malloc.
        void* frames[64];
        int frameCount = backtrace(frames, 64);
        backtrace_symbols_fd(frames, frameCount, g_crashFileFd);

        // Best-effort recent Logger context -- see this file's own
        // header comment on the real, accepted async-signal-safety
        // trade-off here (Logger::recentEntries() allocates).
        writeRaw(g_crashFileFd, "\n--- Recent log entries (may be incomplete/corrupted if the crash was heap-related) ---\n");
        std::vector<LogEntry> entries = Logger::instance().recentEntries(50);
        for (const LogEntry& entry : entries) {
            char line[512];
            std::snprintf(line, sizeof(line), "[%8.3fs][%s][%s] %s\n", static_cast<double>(entry.timestampSeconds),
                          logLevelName(entry.level), entry.category.c_str(), entry.message.c_str());
            writeRaw(g_crashFileFd, line);
        }
        writeRaw(g_crashFileFd, "\n=== end of report ===\n");

        fsync(g_crashFileFd);
    }

    // Real chaining: restore and re-raise so the OS's own default
    // behavior (core dump if ulimit allows, real non-zero exit code)
    // still happens after our report is safely on disk -- a crash
    // reporter that swallows the crash instead of propagating it would
    // be real, honest worse than not installing one at all.
    for (size_t i = 0; i < sizeof(kSignals) / sizeof(kSignals[0]); ++i) {
        if (kSignals[i] == sig) {
            sigaction(sig, &g_previousHandlers[i], nullptr);
            break;
        }
    }
    raise(sig);
}

} // namespace

void installCrashReporter() {
    if (g_installed) return;
    g_installed = true;

    char path[128];
    std::time_t now = std::time(nullptr);
    std::tm tmValue{};
    localtime_r(&now, &tmValue);
    std::snprintf(path, sizeof(path), "crash_report_%04d%02d%02d_%02d%02d%02d.txt", tmValue.tm_year + 1900,
                  tmValue.tm_mon + 1, tmValue.tm_mday, tmValue.tm_hour, tmValue.tm_min, tmValue.tm_sec);
    // Opened once, here, at install() time (not inside the handler) --
    // open() itself is not guaranteed async-signal-safe either; doing it
    // eagerly means the handler only ever does the real signal-safe
    // write()/fsync() to an already-valid fd.
    g_crashFileFd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);

    struct sigaction action{};
    action.sa_handler = &crashHandler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    for (size_t i = 0; i < sizeof(kSignals) / sizeof(kSignals[0]); ++i) {
        sigaction(kSignals[i], &action, &g_previousHandlers[i]);
    }

    logInfo("Crash", "crash reporter installed -- real report path (if needed): %s", path);
}

#else

void installCrashReporter() {
    // Real, honest no-op -- see this function's own header comment on
    // why (Linux/glibc-only for now, not silently pretending to cover
    // Windows too).
    logInfo("Crash", "crash reporter not installed -- this platform isn't Linux/glibc yet");
}

#endif

} // namespace engine::core
