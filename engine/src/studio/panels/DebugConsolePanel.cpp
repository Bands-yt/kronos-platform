#include "studio/panels/DebugConsolePanel.hpp"

#include <imgui.h>
#include <imgui_stdlib.h>

#include "core/Logger.hpp"
#include "studio/StudioEcsScriptApi.hpp"

namespace engine::studio::panels {

namespace {
ImVec4 logLevelColor(core::LogLevel level) {
    switch (level) {
        case core::LogLevel::Debug: return ImVec4(0.55f, 0.58f, 0.62f, 1.0f);
        case core::LogLevel::Info: return ImVec4(0.85f, 0.85f, 0.85f, 1.0f);
        case core::LogLevel::Warn: return ImVec4(0.90f, 0.75f, 0.25f, 1.0f);
        case core::LogLevel::Error: return ImVec4(0.90f, 0.30f, 0.30f, 1.0f);
    }
    return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
}
} // namespace

bool DebugConsolePanel::initialize(core::ECS& ecs) {
    ecs_ = &ecs;
    if (!scripting_.initialize()) return false;

    scripting_.setBindingsHook([this](lua_State* L) { registerStudioEcsBindings(L, *ecs_); });
    scripting_.setOutputCallback([this](const std::string& line) { appendLine(line); });

    history_.push_back("Debug Console ready -- print/engine.log/task.*/events.* and a small ECS-only world.* "
                        "(findByName/getPosition/setPosition/setColor) are available. Enter to run.");
    return true;
}

void DebugConsolePanel::shutdown() { scripting_.shutdown(); }

void DebugConsolePanel::tick(float dt) { scripting_.tick(dt); }

void DebugConsolePanel::appendLine(const std::string& line) {
    history_.push_back(line);
    scrollToBottom_ = true;
    // Kronos (Alpha Completion Checklist, "Crash & Error Telemetry" --
    // "Plugin error routing" / "Script error routing"): also routes
    // through the real, shared core::Logger (category "Console"), the
    // same compile/runtime-error-vs-everything-else classification
    // core::Application's gameplay-script routing and ScriptedPlugin's
    // own routing both already use -- so a REPL mistake shows up in the
    // Engine Log tab (right next to it, in the same window) too.
    if (line.rfind("compile error", 0) == 0 || line.rfind("runtime error", 0) == 0) {
        core::logError("Console", "%s", line.c_str());
    } else {
        core::logInfo("Console", "%s", line.c_str());
    }
}

void DebugConsolePanel::drawReplTab() {
    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImGui::BeginChild("console_history", ImVec2(0.0f, avail.y - 32.0f), ImGuiChildFlags_Borders);
    for (const auto& line : history_) {
        ImGui::TextWrapped("%s", line.c_str());
    }
    if (scrollToBottom_) {
        ImGui::SetScrollHereY(1.0f);
        scrollToBottom_ = false;
    }
    ImGui::EndChild();

    bool runRequested = false;
    ImGui::SetNextItemWidth(avail.x - 70.0f);
    if (ImGui::InputText("##console_input", &inputBuffer_, ImGuiInputTextFlags_EnterReturnsTrue)) {
        runRequested = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Run")) {
        runRequested = true;
    }

    if (runRequested && !inputBuffer_.empty()) {
        appendLine("> " + inputBuffer_);
        scripting_.loadAndRun("Console", inputBuffer_);
        inputBuffer_.clear();
    }
}

void DebugConsolePanel::drawEngineLogTab() {
    core::Logger& logger = core::Logger::instance();

    ImGui::Checkbox("Debug", &showDebugLogs_);
    ImGui::SameLine();
    ImGui::Checkbox("Info", &showInfoLogs_);
    ImGui::SameLine();
    ImGui::Checkbox("Warn", &showWarnLogs_);
    ImGui::SameLine();
    ImGui::Checkbox("Error", &showErrorLogs_);
    ImGui::SameLine();
    if (ImGui::Button("Clear")) logger.clearRingBuffer();

    ImGui::BeginChild("engine_log_entries", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
    // Real, live snapshot -- re-read every frame this tab is visible, not
    // cached, so a log emitted while the tab is open shows up immediately
    // (same "re-run fresh every frame" convention
    // CreatorConsolePlugin::drawPanel() already uses for its own scan).
    std::vector<core::LogEntry> entries = logger.recentEntries();
    for (const auto& entry : entries) {
        if (entry.level == core::LogLevel::Debug && !showDebugLogs_) continue;
        if (entry.level == core::LogLevel::Info && !showInfoLogs_) continue;
        if (entry.level == core::LogLevel::Warn && !showWarnLogs_) continue;
        if (entry.level == core::LogLevel::Error && !showErrorLogs_) continue;
        ImGui::TextColored(logLevelColor(entry.level), "[%.2fs] [%s] [%s] %s", entry.timestampSeconds,
                            core::logLevelName(entry.level), entry.category.c_str(), entry.message.c_str());
    }
    ImGui::EndChild();
}

void DebugConsolePanel::draw() {
    ImGui::Begin("Debug Console");

    if (ImGui::BeginTabBar("debug_console_tabs")) {
        if (ImGui::BeginTabItem("REPL")) {
            drawReplTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Engine Log")) {
            drawEngineLogTab();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::End();
}

} // namespace engine::studio::panels
