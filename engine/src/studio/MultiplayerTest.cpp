#include "studio/MultiplayerTest.hpp"

#include <cctype>
#include <deque>
#include <fstream>
#include <random>

#include "core/LocalGameDirectory.hpp"
#include "core/MyGames.hpp"
#include "core/ProjectFile.hpp"

namespace engine::studio {
namespace {

namespace fs = std::filesystem;

constexpr const char* kServerReady = "headless mode started";
constexpr auto kServerTimeout = std::chrono::seconds(30);

bool fileContains(const fs::path& file, const std::string& text) {
    std::ifstream in(file);
    std::string line;
    while (std::getline(in, line)) {
        if (line.find(text) != std::string::npos) return true;
    }
    return false;
}

} // namespace

bool MultiplayerTest::start(const Request& request, std::string& error) {
    stop();
    request_ = request;
    phase_ = Phase::Failed;
    const fs::path root = request.workRoot;
    if (root.filename().string().rfind("kronos-studio-test", 0) != 0) {
        error = message_ = "the test folder must be named kronos-studio-test...";
        return false;
    }
    std::error_code ec;
    fs::remove_all(root, ec);
    slug_ = core::slugifyGameName(request.gameName);
    const fs::path game = root / "games" / slug_;
    fs::create_directories(game, ec);
    for (const char* folder : {"server", "datastores"}) fs::create_directories(root / folder, ec);
    if (ec) {
        error = message_ = "can't create " + root.string();
        return false;
    }
    const fs::path scene = game / "default.scene";
    if (!request.saveScene || !request.saveScene(scene.string())) {
        error = message_ = "couldn't save the scene for the test";
        return false;
    }
    if (!request.projectDir.empty()) {
        for (const char* folder : {"Scripts", "assets", "audio", "images"}) {
            if (fs::is_directory(request.projectDir / folder, ec)) {
                fs::copy(request.projectDir / folder, game / folder, fs::copy_options::recursive, ec);
            }
        }
        if (fs::is_regular_file(request.projectDir / "asset_map.json", ec)) {
            fs::copy_file(request.projectDir / "asset_map.json", game / "asset_map.json", ec);
        }
    }
    core::ProjectFile project;
    project.name = request.gameName;
    project.scenePaths = {"default.scene"};
    project.activeSceneIndex = 0;
    project.touch();
    const fs::path projectPath = game / "project.project";
    if (!project.saveToFile(projectPath.string()) || !core::writeGameManifestForProject(projectPath.string(), request.gameName)) {
        error = message_ = "couldn't write the test game";
        return false;
    }

    port_ = request.port;
    if (port_ == 0) {
        std::random_device seed;
        port_ = static_cast<uint16_t>(std::uniform_int_distribution<int>(20000, 39999)(seed));
    }
    request_.env.emplace_back("KRONOS_GAMES_DIR", (root / "games").string());
    request_.env.emplace_back("KRONOS_DATASTORE_DIR", (root / "datastores").string());
    request_.env.emplace_back("KRONOS_ALLOW_MULTIPLE", "1");
    // Offline: the test game exists only on this PC.
    request_.env.emplace_back("KRONOS_API_URL", "http://127.0.0.1:9");

    if (!launch("Server", {"--server", std::to_string(port_), "--game", slug_, "--headless"}, error)) {
        message_ = error;
        return false;
    }
    phase_ = Phase::StartingServer;
    message_ = "Starting the server...";
    startedAt_ = std::chrono::steady_clock::now();
    return true;
}

bool MultiplayerTest::launch(const std::string& name, const std::vector<std::string>& args, std::string& error) {
    auto process = std::make_unique<Process>();
    process->name = name;
    std::string folder = name;
    for (char& c : folder) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    const fs::path cwd = request_.workRoot / folder;
    std::error_code ec;
    fs::create_directories(cwd, ec);
    process->logFile = (request_.workRoot / (folder + ".log")).string();
    core::ChildProcess::Options options;
    options.executable = request_.runtime;
    options.args = args;
    options.env = request_.env;
    options.workingDirectory = cwd.string();
    options.logFile = process->logFile;
    if (!process->child.start(options, error)) return false;
    processes_.push_back(std::move(process));
    return true;
}

void MultiplayerTest::update() {
    if (!active() || processes_.empty()) return;
    Process& server = *processes_.front();
    if (!server.child.running()) {
        phase_ = Phase::Failed;
        message_ = "The server " + server.child.describe() + ". Its log: " + server.logFile;
        return;
    }
    if (phase_ != Phase::StartingServer) return;
    if (fileContains(server.logFile, kServerReady)) {
        for (int i = 1; i <= request_.players; ++i) {
            const std::string name = "Player" + std::to_string(i);
            std::vector<std::string> args = {"--client", "127.0.0.1", std::to_string(port_), "--game", slug_, "--name", name};
            if (request_.headlessPlayers) args.push_back("--headless");
            std::string error;
            if (!launch(name, args, error)) {
                phase_ = Phase::Failed;
                message_ = error;
                return;
            }
        }
        phase_ = Phase::Running;
        message_ = "Server on port " + std::to_string(port_) + " with " + std::to_string(request_.players) +
                   (request_.players == 1 ? " player" : " players");
    } else if (std::chrono::steady_clock::now() - startedAt_ > kServerTimeout) {
        phase_ = Phase::Failed;
        message_ = "The server didn't start within 30 seconds. Its log: " + server.logFile;
    }
}

void MultiplayerTest::stop() {
    // Players first, so they don't see the server vanish.
    for (auto it = processes_.rbegin(); it != processes_.rend(); ++it) (*it)->child.stop();
    processes_.clear();
    if (active()) message_ = "Stopped";
    // A failed start keeps its logs, because the message points to them.
    if (phase_ != Phase::Failed) {
        phase_ = Phase::Idle;
        removeWorkFolder();
    }
}

void MultiplayerTest::removeWorkFolder() {
    if (request_.workRoot.empty() || request_.workRoot.filename().string().rfind("kronos-studio-test", 0) != 0) return;
    std::error_code ec;
    fs::remove_all(request_.workRoot, ec);
}

std::vector<MultiplayerTest::Member> MultiplayerTest::members() {
    std::vector<Member> out;
    for (auto& process : processes_) out.push_back({process->name, process->logFile, process->child.describe()});
    return out;
}

std::vector<std::string> MultiplayerTest::tail(const std::string& file, size_t lines) {
    std::ifstream in(file);
    std::deque<std::string> last;
    std::string line;
    while (std::getline(in, line)) {
        last.push_back(line);
        if (last.size() > lines) last.pop_front();
    }
    return {last.begin(), last.end()};
}

} // namespace engine::studio
