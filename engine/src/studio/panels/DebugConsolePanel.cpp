#include "studio/panels/DebugConsolePanel.hpp"

#include <cctype>
#include <cstdio>
#include <optional>
#include <string>

#include <imgui.h>
#include <imgui_stdlib.h>

#include "core/Components.hpp"
#include "core/Logger.hpp"
#include "studio/StudioEcsScriptApi.hpp"
#include "studio/plugins/MovieModePlugin.hpp"

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

// Same linear-scan-by-Name pattern RuntimeAnimationPlayer.cpp's own
// findEntityByName() already uses -- small enough (one loop) that
// duplicating it here beats sharing a header across two otherwise-
// unrelated files for it.
core::EntityId findEntityByName(core::ECS& ecs, const std::string& targetName) {
    for (auto entity : ecs.view<core::Name>()) {
        const auto* nameComp = ecs.tryGetComponent<core::Name>(entity);
        if (nameComp != nullptr && nameComp->value == targetName) return entity;
    }
    return core::kNullEntity;
}

struct ScriptErrorRef {
    std::string entityName;
    int oneBasedLine = 1;
};

// Kronos ("Script Editor QoL" -- Engine Console click-to-jump): parses
// core::Scripting::loadAndRun()'s own real error message shape
// (Scripting.cpp) -- `compile error in "<chunkName>": <lua error>` or
// `runtime error in "<chunkName>": <lua error>`, where `<lua error>` is
// `lua_tostring()`'s own standard Lua/Luau convention,
// `<chunkname>:<line>: <message>`. Searches for `"<chunkName>:"`
// specifically (not just the first digit-colon pattern in the string)
// so a chunk name that itself contains a colon-digit sequence doesn't
// false-match. Returns std::nullopt for any log line that isn't one of
// these two exact prefixes, or whose inner Lua error doesn't carry a
// parseable line number -- an honest "nothing to jump to", not a guess.
std::optional<ScriptErrorRef> parseScriptErrorRef(const std::string& message) {
    const bool isCompileError = message.rfind("compile error in \"", 0) == 0;
    const bool isRuntimeError = message.rfind("runtime error in \"", 0) == 0;
    if (!isCompileError && !isRuntimeError) return std::nullopt;

    const size_t nameStart = message.find('"') + 1;
    const size_t nameEnd = message.find('"', nameStart);
    if (nameEnd == std::string::npos) return std::nullopt;
    const std::string entityName = message.substr(nameStart, nameEnd - nameStart);

    const std::string marker = entityName + ":";
    const size_t markerPos = message.find(marker, nameEnd);
    if (markerPos == std::string::npos) return std::nullopt;

    const size_t digitsStart = markerPos + marker.size();
    size_t digitsEnd = digitsStart;
    while (digitsEnd < message.size() && std::isdigit(static_cast<unsigned char>(message[digitsEnd]))) ++digitsEnd;
    if (digitsEnd == digitsStart) return std::nullopt;

    ScriptErrorRef ref;
    ref.entityName = entityName;
    ref.oneBasedLine = std::stoi(message.substr(digitsStart, digitsEnd - digitsStart));
    return ref;
}
} // namespace

bool DebugConsolePanel::initialize(core::ECS& ecs, plugins::MovieModePlugin& movieMode, core::Renderer& renderer) {
    ecs_ = &ecs;
    if (!scripting_.initialize()) return false;

    scriptMeshApi_ = std::make_unique<core::ScriptMeshApi>(*ecs_);
    scriptCinematicApi_ =
        std::make_unique<ScriptCinematicApi>(movieMode.sequence(), movieMode.rail(), movieMode.exportSettings());
    scriptRenderApi_ = std::make_unique<ScriptRenderApi>(renderer);
    scripting_.setBindingsHook([this](lua_State* L) {
        registerStudioEcsBindings(L, *ecs_);
        scriptMeshApi_->registerInto(L);
        scriptCinematicApi_->registerInto(L);
        scriptRenderApi_->registerInto(L);
    });
    scripting_.setOutputCallback([this](const std::string& line) { appendLine(line); });

    history_.push_back("Debug Console ready -- print/engine.log/task.*/events.*, a small ECS-only world.* "
                        "(findByName/getPosition/setPosition/setColor), mesh.* (beginEditingBox/extrudeFace/"
                        "insetFace/subdivideFace/mergeVertices/setVertexPosition/setVertexUv), cinematic.* "
                        "(addTrack/addKeyframe/sampleChannel/play/setPlayhead/addRailPoint/sampleRail/"
                        "setPhysicalCamera/depthOfFieldRangeMeters/buildExportSchedule/...), and render.* "
                        "(setExposure/exposure/setBloomSettings/bloomSettings/setCinematicMode/"
                        "isCinematicModeEnabled/setDepthOfFieldEnabled/isDepthOfFieldEnabled/setDepthOfFieldParams/"
                        "depthOfFieldParams/setTonemapOperator/tonemapOperator/setColorGradingLutStrength/"
                        "colorGradingLutStrength/loadColorGradingLut/resetColorGradingLutToIdentity -- real, "
                        "numeric/enum tuning knobs only, never a raw Vulkan handle) are available. "
                        "A mesh.* edit shows up in the viewport next frame via Modeling Mode's own re-upload "
                        "sweep; a cinematic.* Transform/LightIntensity track applies live via Movie Mode's own "
                        "update(); a render.* call applies live, the same real Renderer state the Lighting Tools "
                        "panel's own sliders edit. Enter to run.");
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
    for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
        const core::LogEntry& entry = entries[i];
        if (entry.level == core::LogLevel::Debug && !showDebugLogs_) continue;
        if (entry.level == core::LogLevel::Info && !showInfoLogs_) continue;
        if (entry.level == core::LogLevel::Warn && !showWarnLogs_) continue;
        if (entry.level == core::LogLevel::Error && !showErrorLogs_) continue;

        char text[1024];
        std::snprintf(text, sizeof(text), "[%.2fs] [%s] [%s] %s", entry.timestampSeconds,
                      core::logLevelName(entry.level), entry.category.c_str(), entry.message.c_str());

        // Kronos ("Script Editor QoL" -- Engine Console click-to-jump):
        // a script compile/runtime error line whose chunk name resolves
        // back to a live entity becomes a real, clickable Selectable
        // instead of plain, inert text; every other log line (including
        // a script error whose entity has since been deleted/renamed)
        // keeps rendering exactly as before.
        std::optional<ScriptErrorRef> ref = ecs_ != nullptr ? parseScriptErrorRef(entry.message) : std::nullopt;
        core::EntityId jumpEntity = ref ? findEntityByName(*ecs_, ref->entityName) : core::kNullEntity;
        if (ref && jumpEntity != core::kNullEntity) {
            ImGui::PushID(i);
            ImGui::PushStyleColor(ImGuiCol_Text, logLevelColor(entry.level));
            const bool clicked = ImGui::Selectable(text);
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Click to open \"%s\" at line %d", ref->entityName.c_str(), ref->oneBasedLine);
            }
            if (clicked) {
                pendingScriptJump_ = PendingScriptJump{jumpEntity, ref->oneBasedLine};
            }
            ImGui::PopID();
        } else {
            ImGui::TextColored(logLevelColor(entry.level), "%s", text);
        }
    }
    ImGui::EndChild();
}

std::optional<PendingScriptJump> DebugConsolePanel::takePendingScriptJump() {
    if (!pendingScriptJump_) return std::nullopt;
    PendingScriptJump jump = *pendingScriptJump_;
    pendingScriptJump_.reset();
    return jump;
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
