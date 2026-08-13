#include "studio/panels/ScriptEditorPanel.hpp"

#include <cstdio>

#include <imgui.h>
#include <imgui_stdlib.h>

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

void ScriptEditorPanel::draw() {
    ImGui::Begin("Script Editor");
    ImGui::TextDisabled("Backend: %s", backend_->backendName());
    ImGui::Separator();
    backend_->draw();
    ImGui::End();
}

} // namespace engine::studio::panels
