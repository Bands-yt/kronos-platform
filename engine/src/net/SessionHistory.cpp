#include "net/SessionHistory.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace engine::net {

void SessionHistory::recordConnection(const std::string& label, const std::string& address, uint16_t port,
                                       int64_t nowUnixSeconds) {
    auto it = std::find_if(entries_.begin(), entries_.end(),
                            [&](const SessionHistoryEntry& e) { return e.address == address && e.port == port; });
    if (it != entries_.end()) {
        if (!label.empty()) it->label = label;
        it->lastConnectedUnixSeconds = nowUnixSeconds;
        return;
    }
    SessionHistoryEntry entry;
    entry.label = label.empty() ? address : label;
    entry.address = address;
    entry.port = port;
    entry.lastConnectedUnixSeconds = nowUnixSeconds;
    entries_.push_back(std::move(entry));
}

void SessionHistory::removeEntry(const std::string& address, uint16_t port) {
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                   [&](const SessionHistoryEntry& e) { return e.address == address && e.port == port; }),
                   entries_.end());
}

std::vector<SessionHistoryEntry> SessionHistory::entriesMostRecentFirst() const {
    std::vector<SessionHistoryEntry> sorted = entries_;
    std::sort(sorted.begin(), sorted.end(), [](const SessionHistoryEntry& a, const SessionHistoryEntry& b) {
        return a.lastConnectedUnixSeconds > b.lastConnectedUnixSeconds;
    });
    return sorted;
}

bool SessionHistory::saveToFile(const std::string& path) const {
    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) return false;

    out << "SESSIONHISTORY 1\n";
    for (const auto& e : entries_) {
        // label is last on the line (trailing-string convention, same as
        // SceneFile's MESHSOURCE path field) so a real label with spaces
        // round-trips correctly.
        out << "SESSION " << e.address << ' ' << e.port << ' ' << e.lastConnectedUnixSeconds << ' ' << e.label << "\n";
    }
    out << "END\n";
    return out.good();
}

bool SessionHistory::loadFromFile(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) return false;

    std::string header;
    if (!std::getline(in, header) || header.rfind("SESSIONHISTORY", 0) != 0) return false;

    std::vector<SessionHistoryEntry> loaded;
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("SESSION ", 0) == 0) {
            std::istringstream iss(line.substr(8));
            SessionHistoryEntry e;
            int portInt = 0;
            iss >> e.address >> portInt >> e.lastConnectedUnixSeconds;
            e.port = static_cast<uint16_t>(portInt);
            std::string rest;
            std::getline(iss, rest);
            if (!rest.empty() && rest.front() == ' ') rest.erase(rest.begin());
            e.label = rest.empty() ? e.address : rest;
            loaded.push_back(std::move(e));
        } else if (line == "END") {
            break;
        }
    }

    entries_ = std::move(loaded);
    return true;
}

} // namespace engine::net
