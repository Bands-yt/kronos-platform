#include "studio/plugins/ScriptedPlugin.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

#include <imgui.h>

#include "core/AssetCache.hpp"
#include "core/Logger.hpp"
#include "studio/StudioEcsScriptApi.hpp"

namespace engine::studio::plugins {

namespace {

// Entry scripts are small (plugin logic, not asset payloads) -- a whole-
// file slurp needs no streaming/chunked read.
bool readWholeFile(const std::string& path, std::string& outContents) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    outContents = ss.str();
    return true;
}

} // namespace

bool ScriptedPlugin::load(const std::string& manifestPath, core::ECS& ecs, net::NetworkSession& networkSession) {
    ecs_ = &ecs;
    networkSession_ = &networkSession;
    manifestPath_ = manifestPath;
    if (!scriptNetworkApi_) scriptNetworkApi_ = std::make_unique<core::ScriptNetworkApi>(networkSession, scripting_);
    return loadInternal();
}

bool ScriptedPlugin::reload() { return loadInternal(); }

std::string ScriptedPlugin::resolvedScriptPath() const {
    std::filesystem::path manifestDir = std::filesystem::path(manifestPath_).parent_path();
    return (manifestDir / manifest_.entryScript).string();
}

bool ScriptedPlugin::loadInternal() {
    outputLog_.clear();
    lastError_.clear();
    loaded_ = false;

    if (!manifest_.loadFromFile(manifestPath_)) {
        lastError_ = "Failed to read plugin manifest: " + manifestPath_;
        return false;
    }

    std::string scriptPath = resolvedScriptPath();
    std::string source;
    if (!readWholeFile(scriptPath, source)) {
        lastError_ = "Failed to read entry script: " + scriptPath;
        return false;
    }

    // Fresh VM on every (re)load -- no attempt to carry old script state
    // across a reload, matching core::Scripting's own shape: it has no
    // "reset a live VM" operation, only shutdown()+initialize() again.
    // The two hooks below are the exact sandboxing seam gameplay scripts
    // get too (see class comment): a per-VM memory/time budget plus the
    // ECS-only `world` table, nothing weaker built specially for plugins.
    scripting_.shutdown();
    if (!scripting_.initialize()) {
        lastError_ = "Failed to initialize sandboxed Luau VM for plugin";
        return false;
    }
    scripting_.setBindingsHook([this](lua_State* L) {
        registerStudioEcsBindings(L, *ecs_);
        if (scriptNetworkApi_) scriptNetworkApi_->registerInto(L);
    });
    scripting_.setOutputCallback([this](const std::string& line) { appendLine(line); });

    core::ScriptId id = scripting_.loadAndRun(manifest_.name, source);
    if (id == core::kInvalidScript) {
        // loadAndRun() already forwarded the compiler/runtime error into
        // outputLog_ via the output callback set above.
        lastError_ = "Plugin script failed to compile/run -- see output log below";
        return false;
    }

    scriptWriteTimeAtLoad_ = core::fileWriteTimeSeconds(scriptPath);
    loaded_ = true;
    appendLine("Plugin \"" + manifest_.name + "\" loaded from " + scriptPath);
    return true;
}

void ScriptedPlugin::appendLine(const std::string& line) {
    outputLog_.push_back(line);
    // Kronos (Alpha Completion Checklist, "Crash & Error Telemetry" --
    // "Plugin error routing"): also routes through the real, shared
    // core::Logger (category "Plugin:<name>", so the Engine Log/a future
    // crash report can distinguish which plugin logged what) -- the same
    // compile/runtime-error-vs-everything-else classification
    // core::Application's own gameplay-script routing already uses (see
    // Application.cpp). outputLog_ above stays the source of truth for
    // this plugin's own drawPanel() window; this is additive, not a
    // replacement.
    std::string category = "Plugin:" + manifest_.name;
    if (line.rfind("compile error", 0) == 0 || line.rfind("runtime error", 0) == 0) {
        core::logError(category.c_str(), "%s", line.c_str());
    } else {
        core::logInfo(category.c_str(), "%s", line.c_str());
    }
}

bool ScriptedPlugin::scriptChangedOnDisk() const {
    if (!loaded_) return false;
    int64_t currentWriteTime = core::fileWriteTimeSeconds(resolvedScriptPath());
    return core::assetNeedsReload(scriptWriteTimeAtLoad_, currentWriteTime);
}

void ScriptedPlugin::update(float dt, core::ECS& ecs, core::EntityId selected,
                             const std::vector<core::EntityId>& selectedEntities) {
    (void)ecs;
    (void)selected;
    (void)selectedEntities;
    if (loaded_) scripting_.tick(dt);
}

void ScriptedPlugin::drawPanel(core::ECS& ecs, core::EntityId selected,
                                const std::vector<core::EntityId>& selectedEntities) {
    (void)ecs;
    (void)selected;
    (void)selectedEntities;

    ImGui::Begin(manifest_.name.c_str());
    ImGui::TextDisabled("v%s by %s", manifest_.version.c_str(),
                         manifest_.author.empty() ? "unknown" : manifest_.author.c_str());
    if (!manifest_.description.empty()) ImGui::TextWrapped("%s", manifest_.description.c_str());
    ImGui::Separator();

    if (!lastError_.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", lastError_.c_str());
        ImGui::Separator();
    }

    if (scriptChangedOnDisk()) {
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "Script changed on disk.");
        ImGui::SameLine();
        if (ImGui::Button("Reload##scripted_plugin_hot_reload")) (void)reload();
        ImGui::Separator();
    }

    if (ImGui::Button(loaded_ ? "Reload" : "Retry Load")) (void)reload();

    ImGui::BeginChild("scripted_plugin_output", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
    for (const auto& line : outputLog_) {
        ImGui::TextWrapped("%s", line.c_str());
    }
    ImGui::EndChild();

    ImGui::End();
}

} // namespace engine::studio::plugins
