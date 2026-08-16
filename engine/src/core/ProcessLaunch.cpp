#include "core/ProcessLaunch.hpp"

#if defined(__unix__) || defined(__APPLE__)
#include <spawn.h>

extern char** environ;
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

#else // Windows and anything else without posix_spawn -- a real, stated gap, not silently unsupported.

bool launchProcess(const std::string& /*executablePath*/, const std::vector<std::string>& /*args*/) { return false; }

#endif

} // namespace engine::core
