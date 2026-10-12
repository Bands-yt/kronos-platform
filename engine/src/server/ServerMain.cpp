// kronos_server: hosts a game headless. Same simulation code as the Player; no window, no ImGui.
#include "core/Application.hpp"
#include "core/ConsoleQuickEdit.hpp"
#include "core/CrashReporter.hpp"
#include "core/KronosApi.hpp"
#include "core/KronosClientConfig.hpp"
#include "core/Logger.hpp"
#include "core/ResourcePaths.hpp"
#include "publishing/GamePackage.hpp"
#include "runtime/GameLoader.hpp"
#include "runtime/GameLoop.hpp"
#include "tntwars/TntWarsNetRules.hpp"

#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <thread>

int main(int argc, char** argv) {
    using namespace engine;
    core::disableConsoleQuickEditMode();
    if (!core::Logger::instance().enableFileLogging("kronos_server.log")) {
        std::fprintf(stderr, "kronos_server: could not open kronos_server.log, continuing without a log file.\n");
    }
    core::installCrashReporter();

    net::NetworkSession::Config networkConfig;
    networkConfig.mode = net::NetworkMode::Server;
    networkConfig.persistModerationLogs = true;
    std::string gameSlug;
    std::string serverKey;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if ((arg == "--port" || arg == "--server") && i + 1 < argc && argv[i + 1][0] != '-') {
            networkConfig.port = static_cast<uint16_t>(std::atoi(argv[++i]));
        } else if (arg.rfind("--port=", 0) == 0) {
            networkConfig.port = static_cast<uint16_t>(std::atoi(arg.c_str() + 7));
        } else if (arg == "--game" && i + 1 < argc) {
            gameSlug = argv[++i];
        } else if (arg == "--server-key" && i + 1 < argc) {
            serverKey = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            std::printf("kronos_server [--port N] [--game <slug>] [--server-key <key>]\n"
                        "Hosts a game with no window. KRONOS_GAMES_DIR points at the games folder.\n");
            return 0;
        } else if (arg != "--server" && arg != "--headless") {
            std::fprintf(stderr, "kronos_server: ignoring unknown argument \"%s\"\n", arg.c_str());
        }
    }
    std::setvbuf(stdout, nullptr, _IOLBF, BUFSIZ);

    const core::KronosClientConfig clientConfig = core::loadKronosClientConfig(core::executableDirectory());
    std::optional<core::DiscoveredGame> game;
    if (!gameSlug.empty()) {
        const char* gamesDirEnv = std::getenv("KRONOS_GAMES_DIR");
        const std::string gamesDir = (gamesDirEnv != nullptr && gamesDirEnv[0] != '\0')
                                         ? std::string(gamesDirEnv)
                                         : core::resolveResourceDir(core::executableDirectory(), "games", ENGINE_GAMES_DIR);
        core::KronosApi api(clientConfig.apiUrl);
        std::string error;
        game = publishing::resolveCatalogGame(api, gameSlug, gamesDir, publishing::packageCacheDirectory(), error);
        if (!game.has_value()) {
            std::fprintf(stderr, "kronos_server: could not resolve game \"%s\" (%s); hosting an empty ground plane.\n",
                         gameSlug.c_str(), error.c_str());
        }
    }

    core::Application app;
    core::Application::CreateInfo info;
    info.title = "Kronos Server";
    info.headless = true;
    info.startWithCapturedMouse = false;
    if (!app.initialize(info)) {
        std::fprintf(stderr, "kronos_server: Application::initialize failed.\n");
        return 1;
    }
    tntwars::TntWarsNetRules::of(app.networkSession());

    networkConfig.gameName = game.has_value() ? game->manifest.name : "Default World";
    if (!app.startNetworking(networkConfig)) {
        std::fprintf(stderr, "kronos_server: startNetworking failed.\n");
        return 1;
    }
    std::printf("kronos_server: listening on port %u\n", static_cast<unsigned>(networkConfig.port));

    std::thread heartbeatThread;
    if (!serverKey.empty()) {
        core::logInfo("Kronos", "server heartbeat backend: %s (from %s)", clientConfig.apiUrl.c_str(),
                      clientConfig.source.c_str());
        // Every 10 s, against the backend's 30 s TTL: two drops in a row are needed to look dead.
        app.gameLoop()->setPreRenderHook(
            [&app, &heartbeatThread, baseUrl = clientConfig.apiUrl, key = serverKey, accumulated = 0.0f](float dt) mutable {
                accumulated += dt;
                if (accumulated < 10.0f) return;
                accumulated = 0.0f;
                const size_t players = app.networkSession().connectedPeerCount();
                if (heartbeatThread.joinable()) heartbeatThread.join();
                heartbeatThread = std::thread([baseUrl, key, players]() {
                    if (!core::sendServerHeartbeat(baseUrl, key, players)) {
                        std::fprintf(stderr, "kronos_server: heartbeat to \"%s\" failed.\n", baseUrl.c_str());
                    }
                });
            });
    }

    bool hosting = false;
    if (game.has_value()) {
        hosting = runtime::loadGame(app, *game);
        if (hosting) {
            std::printf("kronos_server: dedicated server now hosting \"%s\" (slug \"%s\")\n", game->manifest.name.c_str(), gameSlug.c_str());
        } else {
            std::fprintf(stderr, "kronos_server: failed to load \"%s\"; hosting an empty ground plane.\n", gameSlug.c_str());
        }
    }
    if (!hosting) app.physics().createGroundPlane(app.ecs(), 25.0f, 25.0f);

    app.run();
    if (heartbeatThread.joinable()) heartbeatThread.join();
    app.shutdown();
    return 0;
}
