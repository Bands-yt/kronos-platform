#include "moderation/EscalationEventLog.hpp"

#include <fstream>
#include <sstream>

namespace engine::moderation {

namespace {
int tierToIndex(safety::EscalationTier tier) { return static_cast<int>(tier); }

safety::EscalationTier tierFromIndex(int index) {
    switch (index) {
        case 0: return safety::EscalationTier::Log;
        case 1: return safety::EscalationTier::Mute;
        case 2: return safety::EscalationTier::Restrict;
        case 3: return safety::EscalationTier::HumanReview;
        case 4: return safety::EscalationTier::LegalReport;
        default: return safety::EscalationTier::Log; // unrecognized on load -- fail soft
    }
}
} // namespace

void EscalationEventLog::record(EscalationEvent event) { events_.push_back(std::move(event)); }

bool EscalationEventLog::saveToFile(const std::string& path) const {
    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) return false;

    out << "ESCALATIONLOG 1\n";
    for (const auto& e : events_) {
        // source is last on the line (trailing-string convention).
        out << "EVENT " << e.player << ' ' << tierToIndex(e.tier) << ' ' << e.serverTimestampSeconds << ' ' << e.source
            << "\n";
    }
    out << "END\n";
    return out.good();
}

bool EscalationEventLog::loadFromFile(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) return false;

    std::string header;
    if (!std::getline(in, header) || header.rfind("ESCALATIONLOG", 0) != 0) return false;

    std::vector<EscalationEvent> loaded;
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("EVENT ", 0) == 0) {
            std::istringstream iss(line.substr(6));
            EscalationEvent e;
            int tierIndex = 0;
            iss >> e.player >> tierIndex >> e.serverTimestampSeconds;
            e.tier = tierFromIndex(tierIndex);
            std::string rest;
            std::getline(iss, rest);
            if (!rest.empty() && rest.front() == ' ') rest.erase(rest.begin());
            e.source = rest;
            loaded.push_back(std::move(e));
        } else if (line == "END") {
            break;
        }
    }

    events_ = std::move(loaded);
    return true;
}

} // namespace engine::moderation
