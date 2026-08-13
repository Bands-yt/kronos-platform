#include "studio/PluginChrome.hpp"

#include <imgui.h>

namespace engine::studio {

namespace {
// StudioStyle.cpp's own kAccent is a local baked once into ImGui's color
// table at startup (applyStudioStyle()) -- ImGuiCol_CheckMark is set to
// that exact value and used nowhere accent-sensitive enough to have
// drifted, so reading it back here is the real single source of truth
// rather than a second hardcoded copy of the same RGB triple that could
// silently go stale if the palette ever changes.
ImVec4 accentColor() { return ImGui::GetStyle().Colors[ImGuiCol_CheckMark]; }
} // namespace

void drawPluginHeader(const char* title) {
    ImVec4 accent = accentColor();
    ImGui::TextColored(accent, "%s", title);
    ImVec2 lineStart = ImGui::GetCursorScreenPos();
    float width = ImGui::GetContentRegionAvail().x;
    ImGui::GetWindowDrawList()->AddLine(lineStart, ImVec2(lineStart.x + width, lineStart.y),
                                         ImGui::GetColorU32(accent), 1.5f);
    ImGui::Dummy(ImVec2(0.0f, 6.0f));
}

void drawPluginFooter(const char* statusText) {
    ImGui::Spacing();
    ImGui::Separator();
    if (statusText != nullptr && statusText[0] != '\0') {
        ImGui::TextDisabled("%s", statusText);
    }
}

void helpMarker(const char* text) {
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

} // namespace engine::studio
