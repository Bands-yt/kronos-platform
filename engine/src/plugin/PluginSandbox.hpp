#pragma once

#include <functional>
#include <string>
#include <vector>

#include "plugin/PluginIpc.hpp"

namespace engine::plugin {

// One sandboxed plugin process (kronos_plugin_sandbox). Linux only for now;
// elsewhere spawn() fails and third-party plugins aren't loaded.
class SandboxProcess {
public:
    enum class Result { Ok, Crashed, TimedOut, ProtocolError };

    // Answers one host call. Set `passFd` to hand the plugin a descriptor
    // (it stays owned by the caller). Log messages get no reply.
    using HostCallFn = std::function<void(ipc::Msg type, ipc::Reader& request, ipc::Writer& reply, int& passFd)>;

    SandboxProcess() = default;
    ~SandboxProcess();
    SandboxProcess(const SandboxProcess&) = delete;
    SandboxProcess& operator=(const SandboxProcess&) = delete;

    [[nodiscard]] static bool supported();
    bool spawn(const std::string& executable, const std::string& pluginPath, std::string& error);
    Result request(ipc::Msg type, const std::vector<uint8_t>& payload, std::vector<uint8_t>& reply, int timeoutMs,
                   const HostCallFn& onHostCall);
    // Kills the process if it's still running.
    void terminate();
    [[nodiscard]] bool running() const { return pid_ > 0; }
    // How the process ended: "crashed (Segmentation fault)", "exited with code 3", ...
    [[nodiscard]] const std::string& exitDescription() const { return exitDescription_; }

private:
    void reap(bool kill);

    int pid_ = -1;
    int fd_ = -1;
    std::string exitDescription_;
};

} // namespace engine::plugin
