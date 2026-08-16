#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "moderation/AppealTypes.hpp"

namespace engine::moderation {

// Kronos ("Moderation Architecture v1", Phase 1): real submit/query/
// resolve over real Appeal entries, plus real disk persistence -- same
// "never silently roll off, unlike bounded ChatLog" shape as
// moderation::ReportLog (an appeal, like a report, is a rare, deliberate
// human action that must never vanish before a moderator sees it).
class AppealLog {
public:
    void submit(Appeal appeal);

    [[nodiscard]] const std::vector<Appeal>& appeals() const { return appeals_; }
    [[nodiscard]] size_t size() const { return appeals_.size(); }
    [[nodiscard]] std::vector<Appeal> appealsForPlayer(net::PlayerId player) const;
    // Kronos ("Moderation Architecture v2", "Account System v1" -- "appeal
    // history tied to identity"): the real, cross-session lookup
    // appealsForPlayer() can't do -- PlayerId is a fresh handle every
    // reconnect, this is keyed by the real, stable profileId instead.
    // Real, honest empty result for profileId == 0 ("unknown identity" --
    // see Appeal::profileId's own comment).
    [[nodiscard]] std::vector<Appeal> appealsForProfileId(uint64_t profileId) const;

    // The real moderator action -- sets a real, non-Pending outcome plus
    // a real reviewer note on the appeal at `index` (into appeals(),
    // stable as long as nothing else mutates this log in between, same
    // real index-based-access convention this codebase already accepts
    // for ReviewQueue). A real, honest no-op (returns false) for an
    // out-of-range index -- never throws/asserts on a stale/bad index.
    bool resolve(size_t index, AppealOutcome outcome, const std::string& reviewerNote, double nowServerTimestampSeconds);

    [[nodiscard]] bool saveToFile(const std::string& path) const;
    [[nodiscard]] bool loadFromFile(const std::string& path);

private:
    std::vector<Appeal> appeals_;
};

} // namespace engine::moderation
