#include "studio/plugins/CreatorConsolePlugin.hpp"

#include <imgui.h>

#include "studio/CreatorConsole.hpp"
#include "studio/PluginChrome.hpp"
#include "studio/plugins/TerrainEditorPlugin.hpp"

namespace engine::studio::plugins {

namespace {
ImVec4 severityColor(ConsoleMessageSeverity severity) {
    switch (severity) {
        case ConsoleMessageSeverity::Tip: return ImVec4(0.55f, 0.62f, 0.90f, 1.0f);
        case ConsoleMessageSeverity::Warning: return ImVec4(0.90f, 0.75f, 0.25f, 1.0f);
        case ConsoleMessageSeverity::Error: return ImVec4(0.90f, 0.30f, 0.30f, 1.0f);
    }
    return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
}
const char* severityLabel(ConsoleMessageSeverity severity) {
    switch (severity) {
        case ConsoleMessageSeverity::Tip: return "TIP";
        case ConsoleMessageSeverity::Warning: return "WARN";
        case ConsoleMessageSeverity::Error: return "ERROR";
    }
    return "?";
}
} // namespace

CreatorConsolePlugin::CreatorConsolePlugin(TerrainEditorPlugin& terrainEditor) : terrainEditor_(&terrainEditor) {}

void CreatorConsolePlugin::drawPanel(core::ECS& ecs, core::EntityId, const std::vector<core::EntityId>&) {
    ImGui::Begin(name());
    drawPluginHeader("Creator Console");

    std::vector<ConsoleMessage> messages = scanSceneForMessages(ecs, terrainEditor_->hasTerrain());

    int tipCount = 0, warningCount = 0, errorCount = 0;
    for (const auto& message : messages) {
        if (message.severity == ConsoleMessageSeverity::Tip) ++tipCount;
        else if (message.severity == ConsoleMessageSeverity::Warning) ++warningCount;
        else ++errorCount;
    }

    ImGui::Checkbox("Tips", &showTips_);
    ImGui::SameLine();
    ImGui::TextColored(severityColor(ConsoleMessageSeverity::Tip), "(%d)", tipCount);
    ImGui::SameLine();
    ImGui::Checkbox("Warnings", &showWarnings_);
    ImGui::SameLine();
    ImGui::TextColored(severityColor(ConsoleMessageSeverity::Warning), "(%d)", warningCount);
    ImGui::SameLine();
    ImGui::Checkbox("Errors", &showErrors_);
    ImGui::SameLine();
    ImGui::TextColored(severityColor(ConsoleMessageSeverity::Error), "(%d)", errorCount);

    ImGui::Separator();
    ImGui::BeginChild("##creator_console_scroll", ImVec2(0.0f, 0.0f), false);
    for (const auto& message : messages) {
        if (message.severity == ConsoleMessageSeverity::Tip && !showTips_) continue;
        if (message.severity == ConsoleMessageSeverity::Warning && !showWarnings_) continue;
        if (message.severity == ConsoleMessageSeverity::Error && !showErrors_) continue;
        ImGui::TextColored(severityColor(message.severity), "[%s]", severityLabel(message.severity));
        ImGui::SameLine();
        ImGui::TextWrapped("%s", message.text.c_str());
    }
    if (messages.empty()) {
        ImGui::TextDisabled("No issues found -- the scene looks clean.");
    }
    ImGui::EndChild();

    drawPluginFooter("Live scan, re-run every frame -- not a stale snapshot.");
    ImGui::End();
}

} // namespace engine::studio::plugins
