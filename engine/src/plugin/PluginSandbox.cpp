#include "plugin/PluginSandbox.hpp"

#include <chrono>

#if defined(_WIN32)
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <vector>

#include <aclapi.h>
#include <sddl.h>
#include <userenv.h>
#endif

#if defined(__linux__)
#include <csignal>
#include <cstring>
#include <spawn.h>
#include <sys/wait.h>
#endif

namespace engine::plugin {

SandboxProcess::~SandboxProcess() { terminate(); }

#if defined(__linux__)

bool SandboxProcess::supported() { return true; }

bool SandboxProcess::running() const { return pid_ > 0; }

bool SandboxProcess::spawn(const std::string& executable, const std::string& pluginPath, std::string& error) {
    terminate();
    exitDescription_.clear();
    int sockets[2] = {-1, -1};
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sockets) != 0) {
        error = std::string("could not create the plugin socket: ") + std::strerror(errno);
        return false;
    }
    constexpr int kChildFd = 100;
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, sockets[1], kChildFd);
    const std::string fdArg = std::to_string(kChildFd);
    char* argv[] = {const_cast<char*>(executable.c_str()), const_cast<char*>(fdArg.c_str()),
                    const_cast<char*>(pluginPath.c_str()), nullptr};
    char* envp[] = {nullptr};
    pid_t pid = -1;
    const int spawned = posix_spawn(&pid, executable.c_str(), &actions, nullptr, argv, envp);
    posix_spawn_file_actions_destroy(&actions);
    ::close(sockets[1]);
    if (spawned != 0) {
        ::close(sockets[0]);
        error = "could not start " + executable + ": " + std::strerror(spawned);
        return false;
    }
    pid_ = pid;
    fd_ = sockets[0];
    return true;
}

void SandboxProcess::reap(bool kill) {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    if (pid_ <= 0) return;
    if (kill) ::kill(pid_, SIGKILL);
    int status = 0;
    if (waitpid(pid_, &status, 0) == pid_ && exitDescription_.empty()) {
        if (WIFSIGNALED(status)) {
            const char* name = strsignal(WTERMSIG(status));
            exitDescription_ = std::string("crashed (") + (name ? name : "signal") + ")";
        } else if (WIFEXITED(status)) {
            exitDescription_ = "exited with code " + std::to_string(WEXITSTATUS(status));
        }
    }
    pid_ = -1;
}

void SandboxProcess::terminate() { reap(true); }

bool SandboxProcess::connected() const { return fd_ >= 0; }

bool SandboxProcess::sendTo(ipc::Msg type, const std::vector<uint8_t>& payload, ipc::Passed passed) {
    return ipc::sendFrame(fd_, type, payload, passed);
}

ipc::Recv SandboxProcess::recvFrom(ipc::Msg& type, std::vector<uint8_t>& payload, int timeoutMs) {
    return ipc::recvFrame(fd_, type, payload, timeoutMs);
}

#elif defined(_WIN32)

#include "PluginSandboxWindows.inc"

#else

bool SandboxProcess::supported() { return false; }
bool SandboxProcess::running() const { return false; }
bool SandboxProcess::connected() const { return false; }
bool SandboxProcess::sendTo(ipc::Msg, const std::vector<uint8_t>&, ipc::Passed) { return false; }
ipc::Recv SandboxProcess::recvFrom(ipc::Msg&, std::vector<uint8_t>&, int) { return ipc::Recv::Closed; }

bool SandboxProcess::spawn(const std::string&, const std::string&, std::string& error) {
    error = "sandboxed plugins are not supported on this platform";
    return false;
}
void SandboxProcess::reap(bool) {}
void SandboxProcess::terminate() {}

#endif

SandboxProcess::Result SandboxProcess::request(ipc::Msg type, const std::vector<uint8_t>& payload,
                                               std::vector<uint8_t>& reply, int timeoutMs,
                                               const HostCallFn& onHostCall) {
    if (!connected()) return Result::Crashed;
    if (!sendTo(type, payload, ipc::kNoPassed)) {
        reap(false);
        return Result::Crashed;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    for (;;) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
        if (left.count() <= 0) {
            exitDescription_ = "stopped responding";
            reap(true);
            return Result::TimedOut;
        }
        ipc::Msg incoming{};
        std::vector<uint8_t> bytes;
        switch (recvFrom(incoming, bytes, static_cast<int>(left.count()))) {
        case ipc::Recv::Ok:
            break;
        case ipc::Recv::Timeout:
            exitDescription_ = "stopped responding";
            reap(true);
            return Result::TimedOut;
        case ipc::Recv::Closed:
            reap(false);
            return Result::Crashed;
        case ipc::Recv::Error:
            exitDescription_ = "sent a malformed message";
            reap(true);
            return Result::ProtocolError;
        }
        if (incoming == ipc::Msg::Reply) {
            reply = std::move(bytes);
            return Result::Ok;
        }
        if (incoming < ipc::Msg::Log || incoming > ipc::Msg::SetPosition) {
            exitDescription_ = "sent a malformed message";
            reap(true);
            return Result::ProtocolError;
        }
        ipc::Reader reader(bytes);
        ipc::Writer answer;
        ipc::Passed passed = ipc::kNoPassed;
        onHostCall(incoming, reader, answer, passed);
        if (incoming == ipc::Msg::Log) continue;
        if (!sendTo(ipc::Msg::HostReply, answer.bytes, passed)) {
            reap(false);
            return Result::Crashed;
        }
    }
}


} // namespace engine::plugin
