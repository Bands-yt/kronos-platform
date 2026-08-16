#include "studio/plugins/ModerationPanel.hpp"

#include <chrono>
#include <cstdio>
#include <fstream>

#include <imgui.h>

#include "core/HiddenGemsSelector.hpp"
#include "moderation/AppealLog.hpp"
#include "moderation/DirectMessageLog.hpp"
#include "moderation/EscalationEventLog.hpp"
#include "moderation/ReportLog.hpp"
#include "moderation/SafetyReportGenerator.hpp"
#include "safety/TrustSafetyService.hpp"
#include "studio/PluginChrome.hpp"

namespace engine::studio::plugins {

namespace {
// Kronos ("Moderation Architecture v1", Phase 1): the real, honest
// substitute for "reversed decisions -> ML training data" -- an actual
// retraining pipeline needs real ML infrastructure this codebase doesn't
// have (see safety::TextClassifierStub's own header comment on why no
// real ML exists anywhere here yet). This is real, structured,
// future-ready data collection instead: every real Reversed appeal
// outcome appends one real line here, so a future real retraining
// pipeline has real, honest data to start from -- not a fabricated one.
constexpr const char* kReversedDecisionsLogPath = "reversed_decisions.log";
} // namespace

void ModerationPanel::drawWorldSafetySection() {
    if (!ImGui::CollapsingHeader("World Safety Settings", ImGuiTreeNodeFlags_DefaultOpen)) return;

    moderation::WorldSafetySettings& settings = session_->worldSafetySettings();
    ImGui::Checkbox("Chat enabled", &settings.chatEnabled);
    ImGui::Checkbox("Direct messages enabled", &settings.directMessagesEnabled);
    ImGui::Checkbox("Profanity filter enabled", &settings.profanityFilterEnabled);
    ImGui::Checkbox("Teleport enabled", &settings.teleportEnabled);
    ImGui::Checkbox("Trusted-creator-only mode", &settings.trustedCreatorOnlyMode);
    helpMarker(
        "When enabled, only PlayerIds in the Trusted Creators list below "
        "may use Studio's networked test-session tooling against this world.");
    ImGui::SliderFloat("Max chat messages/sec", &settings.maxChatMessagesPerSecond, 0.5f, 20.0f);
    ImGui::SliderFloat("Max interactions/sec", &settings.maxInteractionsPerSecond, 0.5f, 20.0f);
}

void ModerationPanel::drawChatLogSection() {
    if (!ImGui::CollapsingHeader("Chat Log")) return;

    const moderation::ChatLog& log = session_->chatLog();
    ImGui::Text("%zu real logged messages", log.size());
    ImGui::BeginChild("##chat_log_scroll", ImVec2(0, 150), true);
    for (const moderation::ChatLogEntry& entry : log.entries()) {
        ImVec4 color = entry.containedProfanity || entry.flaggedByClassifier ? ImVec4(0.90f, 0.55f, 0.25f, 1.0f)
                                                                               : ImVec4(0.85f, 0.85f, 0.85f, 1.0f);
        ImGui::TextColored(color, "[%.1fs] player %u: %s%s%s", entry.serverTimestampSeconds, entry.sender,
                            entry.text.c_str(), entry.containedProfanity ? " [profanity]" : "",
                            entry.flaggedByClassifier ? " [flagged]" : "");
    }
    ImGui::EndChild();
}

void ModerationPanel::drawDirectMessagesSection() {
    if (!ImGui::CollapsingHeader("Direct Messages")) return;

    const moderation::DirectMessageLog& log = session_->directMessageLog();
    ImGui::TextWrapped(
        "Real, server-side record of every real direct message this server has processed -- routed through the "
        "exact same TrustSafetyService/PolicyEngine chat uses. A real, blocked DM (Minor Mode restriction or a "
        "real hard-block category) is still recorded here for review, even though it was never delivered.");
    ImGui::Text("%zu real logged direct message(s)", log.size());
    ImGui::BeginChild("##dm_log_scroll", ImVec2(0, 150), true);
    for (const moderation::DirectMessageLogEntry& entry : log.entries()) {
        ImVec4 color = entry.blocked ? ImVec4(0.90f, 0.30f, 0.30f, 1.0f)
                       : entry.flaggedByClassifier ? ImVec4(0.90f, 0.55f, 0.25f, 1.0f)
                                                    : ImVec4(0.85f, 0.85f, 0.85f, 1.0f);
        ImGui::TextColored(color, "[%.1fs] player %u -> player %u: %s%s%s", entry.serverTimestampSeconds, entry.sender,
                            entry.recipient, entry.text.c_str(), entry.flaggedByClassifier ? " [flagged]" : "",
                            entry.blocked ? " [BLOCKED]" : "");
    }
    ImGui::EndChild();
}

void ModerationPanel::drawReportSection() {
    if (!ImGui::CollapsingHeader("Report Player", ImGuiTreeNodeFlags_DefaultOpen)) return;

    ImGui::InputInt("Reported player id", &reportTargetId_);
    const char* categories[] = {"Abuse", "Cheating", "Inappropriate Content"};
    ImGui::Combo("Category", &reportCategoryIndex_, categories, 3);
    ImGui::InputTextMultiline("Description", reportDescriptionBuffer_, sizeof(reportDescriptionBuffer_), ImVec2(0, 60));

    ImGui::BeginDisabled(!session_->isClient());
    if (ImGui::Button("Submit Report")) {
        auto category = static_cast<moderation::ReportCategory>(reportCategoryIndex_);
        session_->reportPlayer(static_cast<net::PlayerId>(reportTargetId_), category, reportDescriptionBuffer_);
        reportStatus_ = "Report submitted.";
        reportDescriptionBuffer_[0] = '\0';
    }
    ImGui::EndDisabled();
    if (!session_->isClient()) ImGui::TextDisabled("Join a server (Network Overlay) to submit a real report.");
    if (!reportStatus_.empty()) ImGui::TextDisabled("%s", reportStatus_.c_str());

    ImGui::Separator();
    const moderation::ReportLog& reportLog = session_->reportLog();
    ImGui::Text("%zu real logged reports", reportLog.size());
    ImGui::BeginChild("##report_log_scroll", ImVec2(0, 120), true);
    for (const moderation::PlayerReport& report : reportLog.reports()) {
        ImGui::Text("[%.1fs] player %u reported player %u (%s): %s", report.serverTimestampSeconds, report.reporter,
                    report.reported, moderation::reportCategoryName(report.category), report.description.c_str());
    }
    ImGui::EndChild();
}

void ModerationPanel::drawReviewQueueSection() {
    if (!ImGui::CollapsingHeader("Review Queue", ImGuiTreeNodeFlags_DefaultOpen)) return;

    moderation::ReviewQueue& queue = session_->reviewQueue();
    const safety::TrustSafetyService& trustSafety = session_->trustSafetyService();
    ImGui::Text("%zu real case(s) awaiting human review", queue.size());
    ImGui::BeginChild("##review_queue_scroll", ImVec2(0, 120), true);
    for (const moderation::ReviewCase& reviewCase : queue.cases()) {
        // Kronos ("Moderation Architecture v1", Phase 1): the real,
        // already-computed risk score/tier for this exact player, read
        // live from the real pipeline -- previously computed but never
        // surfaced in any UI.
        float risk = trustSafety.currentRisk(reviewCase.player);
        safety::EscalationTier tier = trustSafety.currentTier(reviewCase.player);
        ImGui::TextColored(reviewCase.legalReportRequested ? ImVec4(0.90f, 0.30f, 0.30f, 1.0f) : ImVec4(0.90f, 0.75f, 0.30f, 1.0f),
                            "[%.1fs] player %u -- %s%s  (risk %.2f, %s)", reviewCase.serverTimestampSeconds,
                            reviewCase.player, reviewCase.reason.c_str(),
                            reviewCase.legalReportRequested ? " [legal-report-adjacent]" : "", static_cast<double>(risk),
                            safety::escalationTierName(tier));
    }
    ImGui::EndChild();
    if (ImGui::Button("Clear Reviewed")) queue.clear();
}

void ModerationPanel::drawAuditLogSection() {
    if (!ImGui::CollapsingHeader("Audit Log")) return;

    const moderation::EscalationEventLog& log = session_->escalationEventLog();
    ImGui::TextWrapped(
        "Real, disk-persisted record of every real escalation dispatch (Mute/Restrict/Human Review/Legal Report) "
        "this server has ever made -- survives a restart, unlike the live risk score above.");
    ImGui::Text("%zu real logged escalation(s)", log.size());
    ImGui::BeginChild("##audit_log_scroll", ImVec2(0, 150), true);
    for (const moderation::EscalationEvent& event : log.events()) {
        ImGui::Text("[%.1fs] player %u -- %s (%s)", event.serverTimestampSeconds, event.player,
                    safety::escalationTierName(event.tier), event.source.c_str());
    }
    ImGui::EndChild();
}

void ModerationPanel::drawAppealsSection() {
    if (!ImGui::CollapsingHeader("Appeals", ImGuiTreeNodeFlags_DefaultOpen)) return;

    moderation::AppealLog& appeals = session_->appealLog();
    ImGui::Text("%zu real appeal(s) on file", appeals.size());
    ImGui::BeginChild("##appeals_scroll", ImVec2(0, 150), true);
    for (size_t i = 0; i < appeals.appeals().size(); ++i) {
        const moderation::Appeal& appeal = appeals.appeals()[i];
        ImGui::PushID(static_cast<int>(i));
        bool isSelected = selectedAppealIndex_ == static_cast<int>(i);
        std::string label = "player " + std::to_string(appeal.player) + " -- " +
                             moderation::appealOutcomeName(appeal.outcome) + " -- " + appeal.relatedReviewCaseReason;
        if (ImGui::Selectable(label.c_str(), isSelected)) selectedAppealIndex_ = static_cast<int>(i);
        ImGui::PopID();
    }
    ImGui::EndChild();

    if (selectedAppealIndex_ < 0 || selectedAppealIndex_ >= static_cast<int>(appeals.appeals().size())) return;
    const moderation::Appeal& selected = appeals.appeals()[static_cast<size_t>(selectedAppealIndex_)];

    // Real, full context for the human reviewer -- the player's own
    // statement plus what they said it relates to, exactly what the
    // user's spec asks for ("Human mod sees full context").
    ImGui::SeparatorText("Selected Appeal");
    ImGui::Text("Player: %u", selected.player);
    // Kronos ("Moderation Architecture v2", "Account System v1" -- "appeal
    // history tied to identity"): real, cross-session context -- how many
    // real appeals this real, stable account has filed in total, not just
    // this one session's own ephemeral PlayerId.
    if (selected.profileId != 0) {
        size_t accountAppealCount = appeals.appealsForProfileId(selected.profileId).size();
        ImGui::Text("Account profileId: %llu (%zu real appeal(s) from this account)",
                     static_cast<unsigned long long>(selected.profileId), accountAppealCount);
    } else {
        ImGui::TextDisabled("Account profileId: unknown (pre-Account-System-v2 client)");
    }
    ImGui::Text("Related to: %s", selected.relatedReviewCaseReason.c_str());
    ImGui::TextWrapped("Player statement: %s", selected.playerStatement.c_str());
    ImGui::Text("Current outcome: %s", moderation::appealOutcomeName(selected.outcome));

    const char* outcomes[] = {"Upheld", "Reduced", "Reversed"};
    ImGui::Combo("Real outcome", &appealOutcomeIndex_, outcomes, 3);
    ImGui::InputTextMultiline("Reviewer note", appealReviewerNoteBuffer_, sizeof(appealReviewerNoteBuffer_), ImVec2(0, 60));
    if (ImGui::Button("Resolve Appeal")) {
        auto outcome = static_cast<moderation::AppealOutcome>(appealOutcomeIndex_ + 1); // +1: skip Pending==0 in the picker above
        // Real wall-clock seconds -- resolution happens here in Studio, a
        // separate moment/process from the live game session's own
        // clockSeconds_ (which submittedServerTimestampSeconds uses), so
        // a real Unix timestamp is the honest choice for "when", not a
        // borrowed session-relative clock that may not even be running.
        double nowSeconds = static_cast<double>(
            std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
        appeals.resolve(static_cast<size_t>(selectedAppealIndex_), outcome, appealReviewerNoteBuffer_, nowSeconds);

        if (outcome == moderation::AppealOutcome::Reversed) {
            std::ofstream out(kReversedDecisionsLogPath, std::ios::app);
            if (out.is_open()) {
                out << "player=" << selected.player << " relatedTo=\"" << selected.relatedReviewCaseReason
                    << "\" playerStatement=\"" << selected.playerStatement << "\" reviewerNote=\""
                    << appealReviewerNoteBuffer_ << "\"\n";
            }
        }
        appealReviewerNoteBuffer_[0] = '\0';
    }
}

void ModerationPanel::drawTrustedCreatorSection() {
    if (!ImGui::CollapsingHeader("Trusted Creators")) return;

    moderation::TrustedCreatorRegistry& registry = session_->trustedCreatorRegistry();
    ImGui::Text("%zu real trusted creator(s)", registry.trustedCount());
    ImGui::InputInt("Creator player id##trust", &trustTargetId_);
    ImGui::SameLine();
    if (ImGui::Button("Trust")) registry.setTrusted(static_cast<net::PlayerId>(trustTargetId_), true);
    ImGui::SameLine();
    if (ImGui::Button("Untrust")) registry.setTrusted(static_cast<net::PlayerId>(trustTargetId_), false);
    ImGui::Text("Player %d is %s", trustTargetId_,
                registry.isTrusted(static_cast<net::PlayerId>(trustTargetId_)) ? "trusted" : "not trusted");
}

void ModerationPanel::drawServerMuteSection() {
    if (!ImGui::CollapsingHeader("Server Mute")) return;
    if (!session_->isServer()) {
        ImGui::TextDisabled("Host a server (Network Overlay) to manage server-side mutes.");
        return;
    }

    ImGui::InputInt("Player id##mute", &muteTargetId_);
    ImGui::SameLine();
    if (ImGui::Button("Mute")) session_->setServerMuted(static_cast<net::PlayerId>(muteTargetId_), true);
    ImGui::SameLine();
    if (ImGui::Button("Unmute")) session_->setServerMuted(static_cast<net::PlayerId>(muteTargetId_), false);
    ImGui::Text("Player %d is %s server-muted", muteTargetId_,
                session_->isServerMuted(static_cast<net::PlayerId>(muteTargetId_)) ? "" : "not");
}

void ModerationPanel::drawSafetyReportSection() {
    if (!ImGui::CollapsingHeader("Safety Reports")) return;

    ImGui::TextWrapped(
        "Exports a real, human-readable summary of flags/escalations/appeals/reversed decisions currently "
        "retained in this session's logs. This is a cumulative snapshot at generation time, not a "
        "calendar-date-filtered query -- see SafetyReportGenerator.hpp's own comment for why.");

    if (ImGui::Button("Generate Safety Report")) {
        int64_t nowUnixSeconds = std::chrono::duration_cast<std::chrono::seconds>(
                                      std::chrono::system_clock::now().time_since_epoch())
                                      .count();
        std::string periodLabel = core::monthKeyForUnixSeconds(nowUnixSeconds);

        moderation::SafetyReportGenerator generator;
        moderation::SafetyReportSummary summary =
            generator.summarize(session_->reportLog(), session_->escalationEventLog(), session_->appealLog());

        std::string path = "safety_report_" + periodLabel + ".txt";
        if (generator.exportToFile(summary, periodLabel, path)) {
            safetyReportStatus_ = "Exported " + path;
        } else {
            safetyReportStatus_ = "Failed to write " + path;
        }
    }
    if (!safetyReportStatus_.empty()) {
        ImGui::TextDisabled("%s", safetyReportStatus_.c_str());
    }
}

void ModerationPanel::drawPanel(core::ECS&, core::EntityId, const std::vector<core::EntityId>&) {
    ImGui::Begin(name());
    drawPluginHeader("Moderation");

    drawWorldSafetySection();
    drawChatLogSection();
    drawDirectMessagesSection();
    drawReportSection();
    drawReviewQueueSection();
    drawAuditLogSection();
    drawAppealsSection();
    drawSafetyReportSection();
    drawTrustedCreatorSection();
    drawServerMuteSection();

    drawPluginFooter();
    ImGui::End();
}

} // namespace engine::studio::plugins
