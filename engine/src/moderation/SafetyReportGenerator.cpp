#include "moderation/SafetyReportGenerator.hpp"

#include <fstream>
#include <sstream>

namespace engine::moderation {

SafetyReportSummary SafetyReportGenerator::summarize(const ReportLog& reportLog, const EscalationEventLog& escalationLog,
                                                       const AppealLog& appealLog) const {
    SafetyReportSummary summary;

    summary.totalPlayerReports = reportLog.size();
    for (const PlayerReport& report : reportLog.reports()) {
        switch (report.category) {
            case ReportCategory::Abuse: ++summary.abuseReports; break;
            case ReportCategory::Cheating: ++summary.cheatingReports; break;
            case ReportCategory::InappropriateContent: ++summary.inappropriateContentReports; break;
        }
    }

    summary.totalEscalations = escalationLog.size();
    for (const EscalationEvent& event : escalationLog.events()) {
        switch (event.tier) {
            case safety::EscalationTier::Log: break; // never real-logged here -- see EscalationEventLog's own comment
            case safety::EscalationTier::Mute: ++summary.muteEscalations; break;
            case safety::EscalationTier::Restrict: ++summary.restrictEscalations; break;
            case safety::EscalationTier::HumanReview: ++summary.humanReviewEscalations; break;
            case safety::EscalationTier::LegalReport: ++summary.legalReportEscalations; break;
        }
    }

    summary.totalAppeals = appealLog.size();
    for (const Appeal& appeal : appealLog.appeals()) {
        switch (appeal.outcome) {
            case AppealOutcome::Pending: ++summary.pendingAppeals; break;
            case AppealOutcome::Upheld: ++summary.upheldAppeals; break;
            case AppealOutcome::Reduced: ++summary.reducedAppeals; break;
            case AppealOutcome::Reversed: ++summary.reversedAppeals; break;
        }
    }

    return summary;
}

std::string SafetyReportGenerator::formatAsText(const SafetyReportSummary& summary, const std::string& periodLabel) const {
    std::ostringstream out;
    out << "=== Kronos Safety Report: " << periodLabel << " ===\n\n";

    out << "-- Flags (player reports) --\n";
    out << "Total: " << summary.totalPlayerReports << "\n";
    out << "  Abuse: " << summary.abuseReports << "\n";
    out << "  Cheating: " << summary.cheatingReports << "\n";
    out << "  Inappropriate Content: " << summary.inappropriateContentReports << "\n\n";

    out << "-- Escalations --\n";
    out << "Total: " << summary.totalEscalations << "\n";
    out << "  Mute: " << summary.muteEscalations << "\n";
    out << "  Restrict: " << summary.restrictEscalations << "\n";
    out << "  Human Review: " << summary.humanReviewEscalations << "\n";
    out << "  Legal Report: " << summary.legalReportEscalations << "\n\n";

    out << "-- Appeals --\n";
    out << "Total: " << summary.totalAppeals << "\n";
    out << "  Pending: " << summary.pendingAppeals << "\n";
    out << "  Upheld: " << summary.upheldAppeals << "\n";
    out << "  Reduced: " << summary.reducedAppeals << "\n";
    out << "  Reversed (decision overturned): " << summary.reversedAppeals << "\n\n";

    out << "Note: this is a cumulative snapshot of currently-retained records at generation time, not a "
           "calendar-date-filtered query -- see SafetyReportGenerator.hpp's own comment for why.\n";

    return out.str();
}

bool SafetyReportGenerator::exportToFile(const SafetyReportSummary& summary, const std::string& periodLabel,
                                          const std::string& path) const {
    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) return false;
    out << formatAsText(summary, periodLabel);
    return out.good();
}

} // namespace engine::moderation
