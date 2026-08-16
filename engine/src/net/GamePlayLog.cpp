#include "net/GamePlayLog.hpp"

#include <fstream>
#include <sstream>

namespace engine::net {

void GamePlayLog::recordSessionStart(const std::string& gameId, int64_t nowUnixSeconds) {
    GamePlaySession session;
    session.gameId = gameId;
    session.startUnixSeconds = nowUnixSeconds;
    session.endUnixSeconds = 0;
    sessions_.push_back(std::move(session));
}

void GamePlayLog::recordSessionEnd(const std::string& gameId, int64_t nowUnixSeconds, bool crashed) {
    for (auto it = sessions_.rbegin(); it != sessions_.rend(); ++it) {
        if (it->gameId == gameId && it->endUnixSeconds == 0) {
            it->endUnixSeconds = nowUnixSeconds;
            it->crashed = crashed;
            return;
        }
    }
    // Real, honest no-op -- see this method's own header comment.
}

size_t GamePlayLog::reconcileUnclosedSessionsAsCrashed() {
    size_t reconciled = 0;
    for (auto& session : sessions_) {
        if (session.endUnixSeconds == 0) {
            session.endUnixSeconds = session.startUnixSeconds;
            session.crashed = true;
            ++reconciled;
        }
    }
    return reconciled;
}

std::vector<GamePlaySession> GamePlayLog::sessionsForGame(const std::string& gameId) const {
    std::vector<GamePlaySession> matches;
    for (const auto& session : sessions_) {
        if (session.gameId == gameId) matches.push_back(session);
    }
    return matches;
}

bool GamePlayLog::saveToFile(const std::string& path) const {
    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) return false;

    out << "PLAYLOG 1\n";
    for (const auto& s : sessions_) {
        // gameId is last on the line (trailing-string convention, same
        // as SceneFile's MESHSOURCE path field / SessionHistory's label)
        // so a real game name with spaces (e.g. "Sky Garden") round-trips
        // correctly.
        out << "SESSION " << s.startUnixSeconds << ' ' << s.endUnixSeconds << ' ' << (s.crashed ? 1 : 0) << ' '
            << s.gameId << "\n";
    }
    out << "END\n";
    return out.good();
}

bool GamePlayLog::loadFromFile(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) return false;

    std::string header;
    if (!std::getline(in, header) || header.rfind("PLAYLOG", 0) != 0) return false;

    std::vector<GamePlaySession> loaded;
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("SESSION ", 0) == 0) {
            std::istringstream iss(line.substr(8));
            GamePlaySession s;
            int crashedInt = 0;
            iss >> s.startUnixSeconds >> s.endUnixSeconds >> crashedInt;
            s.crashed = crashedInt != 0;
            std::string rest;
            std::getline(iss, rest);
            if (!rest.empty() && rest.front() == ' ') rest.erase(rest.begin());
            s.gameId = rest;
            loaded.push_back(std::move(s));
        } else if (line == "END") {
            break;
        }
        // Any other/unrecognized line is skipped -- forward-compatible,
        // same convention as SceneFile/SessionHistory's own loaders.
    }

    sessions_ = std::move(loaded);
    return true;
}

} // namespace engine::net
