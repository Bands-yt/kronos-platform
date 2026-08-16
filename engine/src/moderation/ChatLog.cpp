#include "moderation/ChatLog.hpp"

#include <fstream>
#include <sstream>

namespace engine::moderation {

void ChatLog::record(ChatLogEntry entry) {
    entries_.push_back(std::move(entry));
    while (entries_.size() > maxEntries_) entries_.pop_front();
}

bool ChatLog::saveToFile(const std::string& path) const {
    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) return false;

    out << "CHATLOG 1\n";
    for (const auto& e : entries_) {
        // text is last on the line (trailing-string convention, same as
        // SceneFile's MESHSOURCE path field) so a real message with
        // spaces round-trips correctly.
        out << "ENTRY " << e.sender << ' ' << (e.containedProfanity ? 1 : 0) << ' ' << (e.flaggedByClassifier ? 1 : 0)
            << ' ' << e.serverTimestampSeconds << ' ' << e.text << "\n";
    }
    out << "END\n";
    return out.good();
}

bool ChatLog::loadFromFile(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) return false;

    std::string header;
    if (!std::getline(in, header) || header.rfind("CHATLOG", 0) != 0) return false;

    std::deque<ChatLogEntry> loaded;
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("ENTRY ", 0) == 0) {
            std::istringstream iss(line.substr(6));
            ChatLogEntry e;
            int profanityInt = 0;
            int flaggedInt = 0;
            iss >> e.sender >> profanityInt >> flaggedInt >> e.serverTimestampSeconds;
            e.containedProfanity = profanityInt != 0;
            e.flaggedByClassifier = flaggedInt != 0;
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
    while (entries_.size() > maxEntries_) entries_.pop_front(); // real, same cap record() always applies
    return true;
}

} // namespace engine::moderation
