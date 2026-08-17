#include "studio/panels/ScriptEditorPanel.hpp"

#include <cstdio>

#include <imgui.h>
#include <imgui_stdlib.h>

#include "core/Components.hpp"
#include "studio/Notification.hpp"

namespace engine::studio::panels {

void ImGuiFallbackEditor::draw() {
    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImGui::InputTextMultiline("##source", &buffer_, avail,
                               ImGuiInputTextFlags_AllowTabInput);
}

bool MonacoWebViewEditor::initialize() {
    std::fprintf(stderr,
                  "MonacoWebViewEditor: not implemented -- Monaco requires an embedded webview (CEF/Ultralight, "
                  "see docs/ARCHITECTURE.md §5); falling back to ImGuiFallbackEditor.\n");
    return false;
}

ScriptEditorPanel::ScriptEditorPanel() {
    auto monaco = std::make_unique<MonacoWebViewEditor>();
    if (monaco->initialize()) {
        backend_ = std::move(monaco); // never reached today -- see MonacoWebViewEditor::initialize()
    } else {
        backend_ = std::make_unique<ImGuiFallbackEditor>();
        backend_->initialize();
    }
}

void ScriptEditorPanel::loadFromEntity(core::ECS& ecs, core::EntityId entity) {
    targetEntity_ = entity;
    if (const core::Script* script = ecs.tryGetComponent<core::Script>(entity)) {
        targetHasScript_ = true;
        backend_->setSource(script->source);
    } else {
        targetHasScript_ = false;
        backend_->setSource("");
    }
}

void ScriptEditorPanel::saveToEntity(core::ECS& ecs, NotificationCenter& notifications) {
    core::Script* script = ecs.tryGetComponent<core::Script>(targetEntity_);
    if (script == nullptr) return;
    // Deliberately only `source` -- `loadedSource` stays whatever it was
    // so core::tickScriptHotReload() sees a real mismatch and does the
    // real (re)load, instead of this save silently marking itself
    // "already loaded" and skipping that step.
    script->source = backend_->source();
    notifications.push("Script saved -- will hot-reload on next tick", NotificationSeverity::Success);
}

void ScriptEditorPanel::draw(core::ECS& ecs, core::EntityId selectedEntity, NotificationCenter& notifications) {
    if (selectedEntity != targetEntity_) {
        loadFromEntity(ecs, selectedEntity);
    }

    ImGui::Begin("Script Editor");

    if (selectedEntity == core::kNullEntity) {
        ImGui::TextDisabled("Select an entity to view or edit its script.");
        ImGui::End();
        return;
    }

    const core::Name* name = ecs.tryGetComponent<core::Name>(selectedEntity);
    ImGui::TextDisabled("Entity: %s", (name != nullptr && !name->value.empty()) ? name->value.c_str() : "(unnamed)");

    if (!targetHasScript_) {
        ImGui::TextWrapped("This entity has no Script component yet.");
        if (ImGui::Button("Add Script Component")) {
            ecs.addComponent<core::Script>(selectedEntity);
            loadFromEntity(ecs, selectedEntity);
        }
        ImGui::End();
        return;
    }

    ImGui::TextDisabled("Backend: %s", backend_->backendName());
    ImGui::SameLine();
    ImGui::TextDisabled("| Ctrl+S to save (real hot-reload while Playing)");
    ImGui::Separator();
    backend_->draw();

    // Checked here, not StudioApp's own global per-frame keybind block,
    // so this only fires while the Script Editor window genuinely has
    // keyboard focus -- Ctrl+S elsewhere in Studio still means "save
    // scene" (StudioApp::run()'s own existing binding), unchanged.
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && ImGui::GetIO().KeyCtrl &&
        ImGui::IsKeyPressed(ImGuiKey_S, false)) {
        saveToEntity(ecs, notifications);
    }

    ImGui::End();
}

} // namespace engine::studio::panels
