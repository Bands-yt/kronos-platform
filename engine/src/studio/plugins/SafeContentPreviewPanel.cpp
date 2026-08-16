#include "studio/plugins/SafeContentPreviewPanel.hpp"

#include <fstream>

#include <imgui.h>

#include "studio/PluginChrome.hpp"

namespace engine::studio::plugins {

namespace {
const char* policyActionLabel(safety::PolicyAction action) {
    switch (action) {
        case safety::PolicyAction::Allow: return "Allow";
        case safety::PolicyAction::Warn: return "Warn";
        case safety::PolicyAction::QueueForReview: return "Queue For Review";
        case safety::PolicyAction::Block: return "Block";
    }
    return "Allow";
}
} // namespace

void SafeContentPreviewPanel::drawSafeTextPreviewSection() {
    if (!ImGui::CollapsingHeader("SafeTextPreview", ImGuiTreeNodeFlags_DefaultOpen)) return;

    ImGui::TextWrapped(
        "Runs your listing title/description (or any creator-authored text) through the same real text "
        "heuristic and policy engine the live server applies to chat, before you publish.");
    ImGui::InputTextMultiline("##textPreviewInput", textPreviewBuffer_, sizeof(textPreviewBuffer_), ImVec2(0, 80));

    std::string text(textPreviewBuffer_);
    if (text.empty()) {
        ImGui::TextDisabled("Type or paste text above to see a real preview.");
        return;
    }

    safety::TextClassification classification = textClassifier_.classify(text);
    if (!classification.flagged) {
        ImGui::TextColored(ImVec4(0.4f, 0.85f, 0.4f, 1.0f), "No real heuristic flags matched.");
        return;
    }

    ImGui::Text("Matched categories:");
    for (safety::TextRiskCategory category : classification.categories) {
        ImGui::BulletText("%s", safety::textRiskCategoryName(category));
    }

    // Real, both real audiences shown side by side -- a creator wants to
    // know whether this passes for everyone, not just the default.
    safety::PolicyDecision adultDecision = policyEngine_.decide(classification, core::AgeGroup::Adult);
    safety::PolicyDecision minorDecision = policyEngine_.decide(classification, core::AgeGroup::Minor);
    ImGui::Text("Adult audience: %s", policyActionLabel(adultDecision.action));
    ImGui::Text("Minor/Unknown audience: %s", policyActionLabel(minorDecision.action));
}

void SafeContentPreviewPanel::drawSafeThumbnailPreviewSection() {
    if (!ImGui::CollapsingHeader("SafeThumbnailPreview")) return;

    ImGui::TextWrapped(
        "Runs a real image file through the same real structural checks and filename heuristic the live "
        "upload path applies, before you publish it.");
    ImGui::InputText("Image path", thumbnailPathBuffer_, sizeof(thumbnailPathBuffer_));

    if (ImGui::Button("Scan Thumbnail")) {
        std::string path(thumbnailPathBuffer_);
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            thumbnailScanStatus_ = "Could not open \"" + path + "\".";
        } else {
            std::vector<uint8_t> fileBytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            size_t dotPos = path.find_last_of('.');
            std::string extension = dotPos != std::string::npos ? path.substr(dotPos + 1) : "";
            size_t slashPos = path.find_last_of("/\\");
            std::string filename = slashPos != std::string::npos ? path.substr(slashPos + 1) : path;

            safety::AssetSafetyReport structuralReport = assetGuard_.scanImageAsset(fileBytes, extension, ipScanner_);
            safety::ImageClassification classification = imageClassifier_.classify(filename);

            if (!structuralReport.anyFlagged() && !classification.flagged) {
                thumbnailScanStatus_ = "No real findings -- structurally clean, no filename markers matched.";
            } else {
                std::string status;
                for (const safety::AssetSafetyFinding& finding : structuralReport.findings) {
                    status += std::string(finding.blocking ? "[BLOCK] " : "[FLAG] ") + finding.description + "\n";
                }
                for (safety::ImageRiskCategory category : classification.categories) {
                    status += std::string("[FLAG] filename matched category: ") +
                              (category == safety::ImageRiskCategory::Nudity           ? "Nudity"
                               : category == safety::ImageRiskCategory::Sexualization  ? "Sexualization"
                               : category == safety::ImageRiskCategory::Gore           ? "Gore"
                                                                                        : "ExtremistSymbols") +
                              "\n";
                }
                thumbnailScanStatus_ = status;
            }
        }
    }
    if (!thumbnailScanStatus_.empty()) {
        ImGui::TextWrapped("%s", thumbnailScanStatus_.c_str());
    }
}

void SafeContentPreviewPanel::drawSafeScriptScanSection() {
    if (!ImGui::CollapsingHeader("SafeScriptScan")) return;

    ImGui::TextWrapped(
        "Scans a script file's own literal source TEXT for the same real keyword markers chat/listing text is "
        "checked against -- catches a harmful string embedded in a UI/dialogue line. This does NOT analyze what "
        "the script's code actually does; that's a real, separate, unbuilt capability, not claimed here.");
    ImGui::InputText("Script path", scriptPathBuffer_, sizeof(scriptPathBuffer_));

    if (ImGui::Button("Scan Script")) {
        std::string path(scriptPathBuffer_);
        std::ifstream file(path);
        if (!file) {
            scriptScanStatus_ = "Could not open \"" + path + "\".";
        } else {
            std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            safety::TextClassification classification = textClassifier_.classify(source);
            if (!classification.flagged) {
                scriptScanStatus_ = "No real heuristic flags matched in this script's source text.";
            } else {
                std::string status = "Matched categories:\n";
                for (safety::TextRiskCategory category : classification.categories) {
                    status += std::string("  ") + safety::textRiskCategoryName(category) + "\n";
                }
                scriptScanStatus_ = status;
            }
        }
    }
    if (!scriptScanStatus_.empty()) {
        ImGui::TextWrapped("%s", scriptScanStatus_.c_str());
    }
}

void SafeContentPreviewPanel::drawPanel(core::ECS&, core::EntityId, const std::vector<core::EntityId>&) {
    ImGui::Begin(name());
    drawPluginHeader("Creator Safety Tools");

    drawSafeTextPreviewSection();
    drawSafeThumbnailPreviewSection();
    drawSafeScriptScanSection();

    drawPluginFooter();
    ImGui::End();
}

} // namespace engine::studio::plugins
