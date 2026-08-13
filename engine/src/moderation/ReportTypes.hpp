#pragma once

#include <string>

#include "net/NetTypes.hpp"

namespace engine::moderation {

// Sprint 12 task 2's "Add categories (abuse, cheating, inappropriate
// content)".
enum class ReportCategory { Abuse, Cheating, InappropriateContent };

[[nodiscard]] const char* reportCategoryName(ReportCategory category);

// A real, player-initiated report -- distinct from
// safety::TrustSafetyService's automated escalation signals
// (onChatMessage/onImageUpload/...): this is a human player flagging
// another human player, not a classifier flagging content. `description`
// is free text the reporter supplies (unlike safety's escalation
// `source` labels, which are fixed subsystem names for the audit trail,
// not user-authored content).
struct PlayerReport {
    net::PlayerId reporter = net::kInvalidPlayer;
    net::PlayerId reported = net::kInvalidPlayer;
    ReportCategory category = ReportCategory::Abuse;
    std::string description;
    double serverTimestampSeconds = 0.0;
};

} // namespace engine::moderation
