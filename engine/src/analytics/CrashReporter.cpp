#include "analytics/CrashReporter.hpp"

#include <cstdio>
#include <csignal>
#include <cstring>

#if defined(__unix__) || defined(__APPLE__)
#define ENGINE_CRASHREPORTER_POSIX 1
#include <fcntl.h>
#include <unistd.h>
#endif

namespace engine::analytics {

std::string CrashReporter::logPath_;

#if ENGINE_CRASHREPORTER_POSIX

void CrashReporter::handleSignal(int signalNumber) {
    // Only async-signal-safe calls beyond this point: no fprintf,
    // no std::string allocation, no iostream. open()/write()/close() are
    // the standard safe primitives for "record that we crashed" handlers.
    const char* name = "unknown signal";
    switch (signalNumber) {
        case SIGSEGV: name = "SIGSEGV"; break;
        case SIGABRT: name = "SIGABRT"; break;
        case SIGFPE: name = "SIGFPE"; break;
        case SIGILL: name = "SIGILL"; break;
        case SIGBUS: name = "SIGBUS"; break;
        default: break;
    }

    int fd = open(logPath_.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd >= 0) {
        // No timestamp -- formatting one safely without libc's non-
        // reentrant strftime/localtime inside a signal handler is more
        // machinery than this marker needs; the log's own mtime after a
        // crash tells you when. A real implementation still wants one
        // (async-signal-safe time formatting is achievable, just not
        // built here).
        const char prefix[] = "[crash] engine terminated by signal: ";
        write(fd, prefix, sizeof(prefix) - 1);
        write(fd, name, std::strlen(name));
        write(fd, "\n", 1);
        close(fd);
    }

    // Restore the default handler and re-raise, so the OS still produces
    // its normal termination behavior (core dump, exit code, whatever the
    // platform does by default) -- this handler observes the crash, it
    // doesn't suppress it.
    std::signal(signalNumber, SIG_DFL);
    std::raise(signalNumber);
}

void CrashReporter::install(std::string crashLogPath) {
    logPath_ = std::move(crashLogPath);
    std::signal(SIGSEGV, &CrashReporter::handleSignal);
    std::signal(SIGABRT, &CrashReporter::handleSignal);
    std::signal(SIGFPE, &CrashReporter::handleSignal);
    std::signal(SIGILL, &CrashReporter::handleSignal);
    std::signal(SIGBUS, &CrashReporter::handleSignal);
    std::fprintf(stdout, "CrashReporter: installed (POSIX signal handler), logging to \"%s\"\n", logPath_.c_str());
}

#else // Windows and anything else without POSIX signals

void CrashReporter::handleSignal(int /*signalNumber*/) {}

void CrashReporter::install(std::string crashLogPath) {
    logPath_ = std::move(crashLogPath);
    // TODO: SetUnhandledExceptionFilter (or vectored exception handling)
    // is the Windows equivalent -- not implemented, see class comment.
    std::fprintf(stderr, "CrashReporter: no handler installed on this platform yet (see CrashReporter.hpp).\n");
}

#endif

void CrashReporter::processPendingCrashReports(const std::string& /*crashLogPath*/) {
    std::fprintf(stdout, "CrashReporter: processPendingCrashReports() not implemented -- see CrashReporter.hpp.\n");
}

} // namespace engine::analytics
