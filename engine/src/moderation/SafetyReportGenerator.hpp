#pragma once

#include <string>

#include "moderation/AppealLog.hpp"
#include "moderation/EscalationEventLog.hpp"
#include "moderation/ReportLog.hpp"

namespace engine::moderation {

// Kronos ("Moderation Architecture v2", item G "Monthly Safety
// Reports"): the real, honest counts this generator can actually
// produce from real, already-persisted logs -- flags (ReportLog),
// escalations by tier (EscalationEventLog), and appeals by outcome
// (AppealLog), exactly the four data sources the user's spec names
// ("summary of flags/escalations/appeals/reversed decisions").
//
// Real, stated scope limit, not a silent one: this is a cumulative
// snapshot of everything currently in each log at generation time, NOT
// a real date-range query filtered to "this calendar month's activity."
// ReportLog::PlayerReport/EscalationEvent both timestamp with the real,
// session-relative serverTimestampSeconds convention (see
// EscalationEventLog.hpp's own comment) -- a real, monotonic per-session
// clock, not a Unix epoch value, so there is no real wall-clock date to
// filter either of those logs by. Only Appeal::resolvedServerTimestampSeconds
// is a real Unix timestamp (set in Studio, which has no live session
// clock -- see AppealTypes.hpp's own comment), and even that alone isn't
// enough to make the other three inputs date-filterable too. "Monthly"
// here means what core::HiddenGemsSelector's own "monthly job" already
// means for this local Alpha (HiddenGemsSelector.hpp's own comment):
// real, calendar-month-labeled, generated on demand or once per real
// month -- not a real historical query, which would need every log this
// pulls from to carry a real wall-clock timestamp, which they honestly
// don't yet.
struct SafetyReportSummary {
    size_t totalPlayerReports = 0;
    size_t abuseReports = 0;
    size_t cheatingReports = 0;
    size_t inappropriateContentReports = 0;

    size_t totalEscalations = 0;
    size_t muteEscalations = 0;
    size_t restrictEscalations = 0;
    size_t humanReviewEscalations = 0;
    size_t legalReportEscalations = 0;

    size_t totalAppeals = 0;
    size_t pendingAppeals = 0;
    size_t upheldAppeals = 0;
    size_t reducedAppeals = 0;
    size_t reversedAppeals = 0;
};

class SafetyReportGenerator {
public:
    // Real, pure reduction over three already-loaded logs -- no disk I/O
    // of its own (the caller already owns/loads these, same "read what's
    // handed in" shape every other real aggregator in this codebase
    // uses, e.g. core::buildGameCatalogueEntries()).
    [[nodiscard]] SafetyReportSummary summarize(const ReportLog& reportLog, const EscalationEventLog& escalationLog,
                                                 const AppealLog& appealLog) const;

    // A real, human-readable text report -- not a reload format (unlike
    // every other moderation:: saveToFile()/loadFromFile() pair, this
    // has no matching load side; it's a document for a person to read,
    // the same real spirit as reversed_decisions.log, just a full
    // summary instead of one line per reversal). `periodLabel` is a
    // real, caller-supplied string (e.g. core::monthKeyForUnixSeconds()'s
    // own "YYYY-MM" output) -- this class has no clock of its own.
    [[nodiscard]] std::string formatAsText(const SafetyReportSummary& summary, const std::string& periodLabel) const;

    // Real, honest `false` if `path` couldn't be opened for write --
    // matches this codebase's "fail soft, say so" save-file convention
    // throughout.
    [[nodiscard]] bool exportToFile(const SafetyReportSummary& summary, const std::string& periodLabel,
                                     const std::string& path) const;
};

} // namespace engine::moderation
