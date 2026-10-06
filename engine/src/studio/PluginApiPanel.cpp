#include "studio/PluginApiPanel.hpp"

#include <imgui.h>

namespace engine::studio {

PluginApiPanel::PluginApiPanel(plugin::PluginHost& host, std::string panelId, std::string title)
    : host_(host), panelId_(std::move(panelId)), title_(std::move(title) + "###pluginpanel." + panelId_) {}

KronosUi PluginApiPanel::imguiTable() {
    KronosUi ui{};
    ui.struct_size = sizeof(KronosUi);
    ui.text = [](void*, const char* text) { ImGui::TextWrapped("%s", text); };
    ui.button = [](void*, const char* label) -> int32_t { return ImGui::Button(label) ? 1 : 0; };
    ui.checkbox = [](void*, const char* label, int32_t* value) -> int32_t {
        bool checked = *value != 0;
        if (!ImGui::Checkbox(label, &checked)) return 0;
        *value = checked ? 1 : 0;
        return 1;
    };
    ui.slider_float = [](void*, const char* label, float* value, float min, float max) -> int32_t {
        return ImGui::SliderFloat(label, value, min, max) ? 1 : 0;
    };
    ui.input_text = [](void*, const char* label, char* buffer, uint32_t capacity) -> int32_t {
        return ImGui::InputText(label, buffer, capacity) ? 1 : 0;
    };
    ui.separator = [](void*) { ImGui::Separator(); };
    ui.same_line = [](void*) { ImGui::SameLine(); };
    return ui;
}

void PluginApiPanel::drawPanel(core::ECS&, core::EntityId, const std::vector<core::EntityId>&) {
    ImGui::SetNextWindowSize(ImVec2(360.0f, 260.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(title_.c_str(), &open_)) {
        ImGui::End();
        return;
    }
    ImGui::PushID(panelId_.c_str());
    static const KronosUi table = imguiTable();
    if (!host_.drawPanel(panelId_, table)) {
        ImGui::TextDisabled("The plugin that provides this panel isn't running.");
        ImGui::TextDisabled("See Resources > Plugins for why.");
    }
    ImGui::PopID();
    ImGui::End();
}

} // namespace engine::studio
