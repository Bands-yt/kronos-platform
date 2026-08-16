#include "moderation/ReviewQueue.hpp"

#include <fstream>
#include <sstream>

namespace engine::moderation {

void ReviewQueue::add(ReviewCase reviewCase) { cases_.push_back(std::move(reviewCase)); }

bool ReviewQueue::saveToFile(const std::string& path) const {
    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) return false;

    out << "REVIEWQUEUE 1\n";
    for (const auto& c : cases_) {
        // reason is last on the line (trailing-string convention).
        out << "CASE " << c.player << ' ' << (c.legalReportRequested ? 1 : 0) << ' ' << c.serverTimestampSeconds << ' '
            << c.reason << "\n";
    }
    out << "END\n";
    return out.good();
}

bool ReviewQueue::loadFromFile(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) return false;

    std::string header;
    if (!std::getline(in, header) || header.rfind("REVIEWQUEUE", 0) != 0) return false;

    std::vector<ReviewCase> loaded;
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("CASE ", 0) == 0) {
            std::istringstream iss(line.substr(5));
            ReviewCase c;
            int legalReportInt = 0;
            iss >> c.player >> legalReportInt >> c.serverTimestampSeconds;
            c.legalReportRequested = legalReportInt != 0;
            std::string rest;
            std::getline(iss, rest);
            if (!rest.empty() && rest.front() == ' ') rest.erase(rest.begin());
            c.reason = rest;
            loaded.push_back(std::move(c));
        } else if (line == "END") {
            break;
        }
    }

    cases_ = std::move(loaded);
    return true;
}

} // namespace engine::moderation
