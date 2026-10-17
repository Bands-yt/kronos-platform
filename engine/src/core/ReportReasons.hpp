#pragma once

#include <string>
#include <vector>

namespace engine::core {

enum class ReportReason { Harassment, HateOrThreats, Sexual, Scam, PersonalInfo, Cheating, Spam, Other };

struct ReportReasonInfo {
    ReportReason reason;
    const char* code;  // stable string sent to the backend
    const char* label; // shown to the player
};

inline const std::vector<ReportReasonInfo>& reportReasons() {
    static const std::vector<ReportReasonInfo> list = {
        {ReportReason::Harassment, "harassment", "Bullying or harassment"},
        {ReportReason::HateOrThreats, "hate_threats", "Hate or threats"},
        {ReportReason::Sexual, "sexual", "Sexual content"},
        {ReportReason::Scam, "scam", "Scam or trick"},
        {ReportReason::PersonalInfo, "personal_info", "Sharing personal information"},
        {ReportReason::Cheating, "cheating", "Cheating or exploiting"},
        {ReportReason::Spam, "spam", "Spam"},
        {ReportReason::Other, "other", "Something else"},
    };
    return list;
}

inline const char* reportReasonCode(ReportReason r) {
    for (const auto& i : reportReasons()) {
        if (i.reason == r) return i.code;
    }
    return "other";
}

// Unknown codes are rejected (not mapped to Other) so a typo is noticed.
inline bool parseReportReason(const std::string& code, ReportReason& out) {
    for (const auto& i : reportReasons()) {
        if (code == i.code) {
            out = i.reason;
            return true;
        }
    }
    return false;
}

} // namespace engine::core
