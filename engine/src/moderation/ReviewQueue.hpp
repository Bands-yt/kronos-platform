#pragma once

#include <string>
#include <vector>

#include "net/NetTypes.hpp"

namespace engine::moderation {

// The real destination for safety::TrustSafetyService::Callbacks::
// onHumanReviewRequired/onLegalReportRequired -- those callbacks existed
// since before this sprint but had nothing real to call (see
// TrustSafetyService.hpp's own comment: "none of Players/moderator-
// tooling/legal-reporting-integration exist yet... for this class to
// call directly"). This is the moderator-tooling half of that gap: a
// real, append-only queue a creator/moderator reviews via
// studio::plugins::ModerationPanel. `legalReportRequested` mirrors
// dispatchEscalation()'s own real behavior (TrustSafetyService.cpp): a
// LegalReport-tier signal still only ever reaches onHumanReviewRequired
// automatically -- a human confirms before anything resembling an actual
// legal report happens, so this flag is a real "flagged as
// legal-report-adjacent, needs the human's attention first" marker, not
// a claim that a report was filed.
struct ReviewCase {
    net::PlayerId player = net::kInvalidPlayer;
    std::string reason;
    bool legalReportRequested = false;
    double serverTimestampSeconds = 0.0;
};

class ReviewQueue {
public:
    void add(ReviewCase reviewCase);

    [[nodiscard]] const std::vector<ReviewCase>& cases() const { return cases_; }
    [[nodiscard]] size_t size() const { return cases_.size(); }

    // A creator/moderator marks the queue reviewed -- real, explicit,
    // never automatic (nothing in this codebase ever silently clears a
    // pending review case).
    void clear() { cases_.clear(); }

    // Kronos ("Moderation Architecture v1", Phase 1): real disk
    // persistence, same convention as moderation::ChatLog/ReportLog's
    // own saveToFile()/loadFromFile() -- a pending human-review case
    // must survive a process restart, not silently vanish.
    [[nodiscard]] bool saveToFile(const std::string& path) const;
    [[nodiscard]] bool loadFromFile(const std::string& path);

private:
    std::vector<ReviewCase> cases_;
};

} // namespace engine::moderation
