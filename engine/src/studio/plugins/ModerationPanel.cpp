#include "studio/plugins/ModerationPanel.hpp"

#include <cstdio>

#include <imgui.h>

#include "moderation/ReportLog.hpp"
#include "studio/PluginChrome.hpp"

namespace engine::studio::plugins {

void ModerationPanel::drawWorldSafetySection() {
    if (!ImGui::CollapsingHeader("World Safety Settings", ImGuiTreeNodeFlags_DefaultOpen)) return;

    moderation::WorldSafetySettings& settings = session_->worldSafetySettings();
    ImGui::Checkbox("Chat enabled", &settings.chatEnabled);
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
    ImGui::Text("%zu real case(s) awaiting human review", queue.size());
    ImGui::BeginChild("##review_queue_scroll", ImVec2(0, 120), true);
    for (const moderation::ReviewCase& reviewCase : queue.cases()) {
        ImGui::TextColored(reviewCase.legalReportRequested ? ImVec4(0.90f, 0.30f, 0.30f, 1.0f) : ImVec4(0.90f, 0.75f, 0.30f, 1.0f),
                            "[%.1fs] player %u -- %s%s", reviewCase.serverTimestampSeconds, reviewCase.player,
                            reviewCase.reason.c_str(), reviewCase.legalReportRequested ? " [legal-report-adjacent]" : "");
    }
    ImGui::EndChild();
    if (ImGui::Button("Clear Reviewed")) queue.clear();
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

void ModerationPanel::drawPanel(core::ECS&, core::EntityId, const std::vector<core::EntityId>&) {
    ImGui::Begin(name());
    drawPluginHeader("Moderation");

    drawWorldSafetySection();
    drawChatLogSection();
    drawReportSection();
    drawReviewQueueSection();
    drawTrustedCreatorSection();
    drawServerMuteSection();

    drawPluginFooter();
    ImGui::End();
}

} // namespace engine::studio::plugins
