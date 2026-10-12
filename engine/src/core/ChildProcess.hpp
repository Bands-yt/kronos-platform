#pragma once

#include <string>
#include <utility>
#include <vector>

namespace engine::core {

// A program this process starts, watches and stops (Studio's test server and
// players). Output and errors go to `logFile`. Stopped when destroyed.
class ChildProcess {
public:
    struct Options {
        std::string executable;
        std::vector<std::string> args;
        std::vector<std::pair<std::string, std::string>> env; // added to this process's environment
        std::string workingDirectory;                         // empty: this process's
        std::string logFile;                                  // empty: inherit output
    };

    ChildProcess() = default;
    ~ChildProcess();
    ChildProcess(const ChildProcess&) = delete;
    ChildProcess& operator=(const ChildProcess&) = delete;
    ChildProcess(ChildProcess&& other) noexcept;
    ChildProcess& operator=(ChildProcess&& other) noexcept;

    [[nodiscard]] bool start(const Options& options, std::string& error);
    // Checks without waiting; false once it has exited (or never started).
    [[nodiscard]] bool running();
    // After it exited: the exit code, or -signal on POSIX.
    [[nodiscard]] int exitStatus() const { return status_; }
    // "exited with code 3", "killed by signal 11", "running", ...
    [[nodiscard]] std::string describe();
    // Asks it to stop, waits up to `graceMs`, then kills it.
    void stop(int graceMs = 2000);

private:
#if defined(_WIN32)
    void* process_ = nullptr;
#else
    int pid_ = 0;
#endif
    bool started_ = false;
    bool exited_ = false;
    int status_ = 0;
};

} // namespace engine::core
