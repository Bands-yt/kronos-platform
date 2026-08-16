#include "moderation/ReportLog.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace engine::moderation {

const char* reportCategoryName(ReportCategory category) {
    switch (category) {
        case ReportCategory::Abuse: return "Abuse";
        case ReportCategory::Cheating: return "Cheating";
        case ReportCategory::InappropriateContent: return "Inappropriate Content";
    }
    return "Unknown";
}

namespace {
int reportCategoryToIndex(ReportCategory category) { return static_cast<int>(category); }

ReportCategory reportCategoryFromIndex(int index) {
    switch (index) {
        case 0: return ReportCategory::Abuse;
        case 1: return ReportCategory::Cheating;
        case 2: return ReportCategory::InappropriateContent;
        default: return ReportCategory::Abuse; // unrecognized on load -- fail soft
    }
}
} // namespace

void ReportLog::submit(PlayerReport report) { reports_.push_back(std::move(report)); }

std::vector<PlayerReport> ReportLog::reportsAgainst(net::PlayerId reported) const {
    std::vector<PlayerReport> result;
    for (const auto& report : reports_) {
        if (report.reported == reported) result.push_back(report);
    }
    return result;
}

size_t ReportLog::countAgainst(net::PlayerId reported) const {
    return static_cast<size_t>(std::count_if(reports_.begin(), reports_.end(),
                                              [reported](const PlayerReport& r) { return r.reported == reported; }));
}

bool ReportLog::saveToFile(const std::string& path) const {
    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) return false;

    out << "REPORTLOG 1\n";
    for (const auto& r : reports_) {
        // description is last on the line (trailing-string convention).
        out << "REPORT " << r.reporter << ' ' << r.reported << ' ' << reportCategoryToIndex(r.category) << ' '
            << r.serverTimestampSeconds << ' ' << r.description << "\n";
    }
    out << "END\n";
    return out.good();
}

bool ReportLog::loadFromFile(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) return false;

    std::string header;
    if (!std::getline(in, header) || header.rfind("REPORTLOG", 0) != 0) return false;

    std::vector<PlayerReport> loaded;
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("REPORT ", 0) == 0) {
            std::istringstream iss(line.substr(7));
            PlayerReport r;
            int categoryIndex = 0;
            iss >> r.reporter >> r.reported >> categoryIndex >> r.serverTimestampSeconds;
            r.category = reportCategoryFromIndex(categoryIndex);
            std::string rest;
            std::getline(iss, rest);
            if (!rest.empty() && rest.front() == ' ') rest.erase(rest.begin());
            r.description = rest;
            loaded.push_back(std::move(r));
        } else if (line == "END") {
            break;
        }
    }

    reports_ = std::move(loaded);
    return true;
}

} // namespace engine::moderation
