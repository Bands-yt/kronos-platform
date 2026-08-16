#include "moderation/DirectMessageLog.hpp"

#include <fstream>
#include <sstream>

namespace engine::moderation {

void DirectMessageLog::record(DirectMessageLogEntry entry) {
    entries_.push_back(std::move(entry));
    while (entries_.size() > maxEntries_) entries_.pop_front();
}

bool DirectMessageLog::saveToFile(const std::string& path) const {
    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) return false;

    out << "DMLOG 1\n";
    for (const auto& e : entries_) {
        // text is last on the line (trailing-string convention).
        out << "ENTRY " << e.sender << ' ' << e.recipient << ' ' << (e.flaggedByClassifier ? 1 : 0) << ' '
            << (e.blocked ? 1 : 0) << ' ' << e.serverTimestampSeconds << ' ' << e.text << "\n";
    }
    out << "END\n";
    return out.good();
}

bool DirectMessageLog::loadFromFile(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) return false;

    std::string header;
    if (!std::getline(in, header) || header.rfind("DMLOG", 0) != 0) return false;

    std::deque<DirectMessageLogEntry> loaded;
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("ENTRY ", 0) == 0) {
            std::istringstream iss(line.substr(6));
            DirectMessageLogEntry e;
            int flaggedInt = 0;
            int blockedInt = 0;
            iss >> e.sender >> e.recipient >> flaggedInt >> blockedInt >> e.serverTimestampSeconds;
            e.flaggedByClassifier = flaggedInt != 0;
            e.blocked = blockedInt != 0;
            std::string rest;
            std::getline(iss, rest);
            if (!rest.empty() && rest.front() == ' ') rest.erase(rest.begin());
            e.text = rest;
            loaded.push_back(std::move(e));
        } else if (line == "END") {
            break;
        }
    }

    entries_ = std::move(loaded);
    while (entries_.size() > maxEntries_) entries_.pop_front();
    return true;
}

} // namespace engine::moderation
