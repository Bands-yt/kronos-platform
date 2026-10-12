#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "core/ChildProcess.hpp"

namespace engine::studio {

// Studio's "test with a server": saves the open scene as a temporary game,
// starts a windowless server on this PC, then N Player windows that join it,
// like Roblox Studio's Start Server + Players.
class MultiplayerTest {
public:
    struct Request {
        std::string runtime;             // engine_runtime
        std::filesystem::path workRoot;  // its last part must start with "kronos-studio-test"; emptied first
        std::string gameName = "Studio Test";
        std::function<bool(const std::string& scenePath)> saveScene;
        std::filesystem::path projectDir; // Scripts/, assets/, audio/, images/ and asset_map.json are copied
        int players = 2;
        uint16_t port = 0; // 0 picks one
        bool headlessPlayers = false;
        std::vector<std::pair<std::string, std::string>> env; // for every process
    };
    enum class Phase { Idle, StartingServer, Running, Failed };
    struct Member {
        std::string name;
        std::string logFile;
        std::string status;
    };

    ~MultiplayerTest() {
        stop();
        removeWorkFolder();
    }

    [[nodiscard]] bool start(const Request& request, std::string& error);
    // Starts the players once the server is up; notices crashes. Call often.
    void update();
    void stop();

    [[nodiscard]] Phase phase() const { return phase_; }
    [[nodiscard]] bool active() const { return phase_ == Phase::StartingServer || phase_ == Phase::Running; }
    [[nodiscard]] const std::string& message() const { return message_; }
    [[nodiscard]] uint16_t port() const { return port_; }
    [[nodiscard]] std::vector<Member> members();
    [[nodiscard]] static std::vector<std::string> tail(const std::string& file, size_t lines);

private:
    struct Process {
        std::string name;
        std::string logFile;
        core::ChildProcess child;
    };
    bool launch(const std::string& name, const std::vector<std::string>& args, std::string& error);
    void removeWorkFolder();

    Request request_;
    Phase phase_ = Phase::Idle;
    std::string message_;
    uint16_t port_ = 0;
    std::string slug_;
    std::vector<std::unique_ptr<Process>> processes_;
    std::chrono::steady_clock::time_point startedAt_{};
};

} // namespace engine::studio
