#pragma once

#include <vector>

#include "moderation/ReportTypes.hpp"

namespace engine::moderation {

// Sprint 12 task 2's "Add server-side report logging + timestamps" --
// real submit/query over real PlayerReport entries. Not bounded/ring-
// buffered like ChatLog: reports are comparatively rare (a human
// deliberately filing one, not every chat message) and are exactly the
// kind of record that should never silently roll off before a creator/
// moderator reviews it.
class ReportLog {
public:
    void submit(PlayerReport report);

    [[nodiscard]] const std::vector<PlayerReport>& reports() const { return reports_; }
    [[nodiscard]] size_t size() const { return reports_.size(); }

    // Real, targeted query -- what a ModerationPanel or an automated
    // "N reports against this player" anti-cheat-adjacent signal reads.
    [[nodiscard]] std::vector<PlayerReport> reportsAgainst(net::PlayerId reported) const;
    [[nodiscard]] size_t countAgainst(net::PlayerId reported) const;

private:
    std::vector<PlayerReport> reports_;
};

} // namespace engine::moderation
