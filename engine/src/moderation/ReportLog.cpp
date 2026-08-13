#include "moderation/ReportLog.hpp"

#include <algorithm>

namespace engine::moderation {

const char* reportCategoryName(ReportCategory category) {
    switch (category) {
        case ReportCategory::Abuse: return "Abuse";
        case ReportCategory::Cheating: return "Cheating";
        case ReportCategory::InappropriateContent: return "Inappropriate Content";
    }
    return "Unknown";
}

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

} // namespace engine::moderation
