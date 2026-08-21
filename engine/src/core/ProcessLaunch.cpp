#include "core/ProcessLaunch.hpp"

#if defined(__unix__) || defined(__APPLE__)
#include <spawn.h>
#include <unistd.h>

extern char** environ;
#elif defined(_WIN32)
#include <windows.h>
#endif

namespace engine::core {

#if defined(__unix__) || defined(__APPLE__)

bool launchProcess(const std::string& executablePath, const std::vector<std::string>& args) {
    // posix_spawn's argv wants char*const[], not const char*const[] --
    // real POSIX API wart (it never actually writes through these), not
    // a sign anything here needs to be mutable. std::string::data() is
    // guaranteed null-terminated and non-const since C++17, the
    // standard, well-established way to satisfy this exact signature
    // without an extra copy.
    std::vector<char*> argv;
    argv.reserve(args.size() + 2);
    argv.push_back(const_cast<char*>(executablePath.c_str()));
    for (const std::string& arg : args) argv.push_back(const_cast<char*>(arg.data()));
    argv.push_back(nullptr);

    pid_t pid = 0;
    int result = posix_spawn(&pid, executablePath.c_str(), nullptr, nullptr, argv.data(), environ);
    if (result != 0) return false;

    // Real fire-and-forget -- see this function's own header comment.
    // Deliberately never wait()s on `pid`: this process never asks
    // whether the child later exits or how, and both real call sites are
    // themselves short-lived within this process's own remaining
    // lifetime (Studio keeps running independently once launched; a
    // CLI-flag relaunch of engine_runtime is immediately followed by
    // this process exiting) -- a real, small, explicitly-stated gap, not
    // a silent one: the spawned child becomes a zombie entry until this
    // process itself exits and the OS reaps it, rather than a leak that
    // outlives this process.
    (void)pid;
    return true;
}

#elif defined(_WIN32)

bool launchProcess(const std::string& executablePath, const std::vector<std::string>& args) {
    // Windows has no argv array in its process-creation API at all -- a
    // real command line is ONE string the child re-parses itself, so
    // each argument has to be quoted here or an argument containing a
    // space would silently arrive as two. Backslashes immediately before
    // a quote also have to be doubled, per the real rules the C runtime's
    // own command-line parser uses.
    auto quoteArgument = [](const std::string& arg) {
        std::string quoted = "\"";
        size_t i = 0;
        while (i < arg.size()) {
            size_t backslashes = 0;
            while (i < arg.size() && arg[i] == '\\') {
                ++backslashes;
                ++i;
            }
            if (i == arg.size()) {
                // These backslashes immediately precede the closing quote,
                // so they must be doubled -- otherwise they would escape
                // that quote and swallow the end of the argument.
                quoted.append(backslashes * 2, '\\');
            } else if (arg[i] == '"') {
                // Double the run, then add one more to escape this quote.
                quoted.append(backslashes * 2 + 1, '\\');
                quoted += '"';
                ++i;
            } else {
                // Backslashes not followed by a quote are literal.
                quoted.append(backslashes, '\\');
                quoted += arg[i];
                ++i;
            }
        }
        quoted += '"';
        return quoted;
    };

    std::string commandLine = quoteArgument(executablePath);
    for (const std::string& arg : args) commandLine += " " + quoteArgument(arg);

    // CreateProcessW/A may write into the command-line buffer in place,
    // so it needs real mutable storage rather than the string's own.
    std::vector<char> mutableCommandLine(commandLine.begin(), commandLine.end());
    mutableCommandLine.push_back('\0');

    STARTUPINFOA startupInfo;
    ZeroMemory(&startupInfo, sizeof(startupInfo));
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo;
    ZeroMemory(&processInfo, sizeof(processInfo));

    BOOL created = CreateProcessA(executablePath.c_str(), mutableCommandLine.data(), nullptr, nullptr, FALSE,
                                   DETACHED_PROCESS, nullptr, nullptr, &startupInfo, &processInfo);
    if (!created) return false;

    // Real fire-and-forget, same contract as the POSIX path above.
    // Closing these two handles releases this process's own references
    // to the child without terminating it -- and, unlike POSIX, leaves
    // no zombie entry behind, since Windows reclaims the process object
    // once the last handle to it is closed.
    CloseHandle(processInfo.hProcess);
    CloseHandle(processInfo.hThread);
    return true;
}

#else // Any other platform without posix_spawn -- a real, stated gap, not silently unsupported.

bool launchProcess(const std::string& /*executablePath*/, const std::vector<std::string>& /*args*/) { return false; }

#endif

int64_t currentProcessId() {
#if defined(_WIN32)
    return static_cast<int64_t>(GetCurrentProcessId());
#else
    return static_cast<int64_t>(::getpid());
#endif
}

} // namespace engine::core
