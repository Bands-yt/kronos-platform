#include "core/UITheme.hpp"

#include <cstdio>

#include <imgui.h>

namespace engine::core {

namespace {
ImFont* g_boldFont = nullptr;
} // namespace

void applyKronosUITheme() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    // A single teal/cyan accent, deliberately not ImGui's stock blue or
    // Blender-orange -- picked to sit apart from ImGuizmo's own X/Y/Z
    // (red/green/blue) axis colors so the gizmo always reads as distinct
    // from surrounding UI chrome.
    const ImVec4 kAccent(0.25f, 0.62f, 0.60f, 1.00f);
    const ImVec4 kAccentHovered(0.33f, 0.72f, 0.70f, 1.00f);
    const ImVec4 kAccentActive(0.19f, 0.50f, 0.48f, 1.00f);
    const ImVec4 kBgDarkest(0.086f, 0.094f, 0.106f, 1.00f);
    const ImVec4 kBgDark(0.106f, 0.114f, 0.128f, 1.00f);
    const ImVec4 kBgMed(0.140f, 0.150f, 0.167f, 1.00f);
    const ImVec4 kBgLight(0.178f, 0.190f, 0.210f, 1.00f);
    const ImVec4 kBorder(0.26f, 0.28f, 0.32f, 0.55f);
    const ImVec4 kText(0.90f, 0.91f, 0.93f, 1.00f);
    const ImVec4 kTextDisabled(0.50f, 0.52f, 0.56f, 1.00f);

    colors[ImGuiCol_Text] = kText;
    colors[ImGuiCol_TextDisabled] = kTextDisabled;
    colors[ImGuiCol_WindowBg] = kBgDark;
    // Kronos ("UI/UX Revamp" -- "subtle background contrast between
    // panels"): a real, small step away from fully transparent (this was
    // ImVec4(0,0,0,0) -- "inherit parent" -- until this pass). A low,
    // deliberately subtle alpha reads as "this is a distinct region" on
    // top of WindowBg without recreating the "boxed-in-boxes" look the
    // original fully-transparent choice was avoiding -- nested child
    // regions (Explorer rows, Inspector sections, scrollable log panes)
    // now have a real, gentle depth cue instead of none at all.
    colors[ImGuiCol_ChildBg] = ImVec4(kBgDarkest.x, kBgDarkest.y, kBgDarkest.z, 0.35f);
    colors[ImGuiCol_PopupBg] = ImVec4(kBgMed.x, kBgMed.y, kBgMed.z, 0.98f);
    colors[ImGuiCol_Border] = kBorder;
    colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

    colors[ImGuiCol_FrameBg] = kBgLight;
    colors[ImGuiCol_FrameBgHovered] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.28f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.42f);

    colors[ImGuiCol_TitleBg] = kBgDarkest;
    colors[ImGuiCol_TitleBgActive] = kBgMed;
    colors[ImGuiCol_TitleBgCollapsed] = kBgDarkest;
    colors[ImGuiCol_MenuBarBg] = kBgDarkest;

    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_ScrollbarGrab] = kBgLight;
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.55f);
    colors[ImGuiCol_ScrollbarGrabActive] = kAccentActive;

    colors[ImGuiCol_CheckMark] = kAccent;
    colors[ImGuiCol_SliderGrab] = kAccent;
    colors[ImGuiCol_SliderGrabActive] = kAccentActive;

    colors[ImGuiCol_Button] = kBgLight;
    colors[ImGuiCol_ButtonHovered] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.55f);
    colors[ImGuiCol_ButtonActive] = kAccentActive;

    colors[ImGuiCol_Header] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.28f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.45f);
    colors[ImGuiCol_HeaderActive] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.60f);

    colors[ImGuiCol_Separator] = kBorder;
    colors[ImGuiCol_SeparatorHovered] = kAccentHovered;
    colors[ImGuiCol_SeparatorActive] = kAccentActive;

    colors[ImGuiCol_ResizeGrip] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.20f);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.55f);
    colors[ImGuiCol_ResizeGripActive] = kAccentActive;

    colors[ImGuiCol_Tab] = kBgMed;
    colors[ImGuiCol_TabHovered] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.65f);
    colors[ImGuiCol_TabSelected] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.75f);
    colors[ImGuiCol_TabSelectedOverline] = kAccent;
    colors[ImGuiCol_TabDimmed] = kBgDark;
    colors[ImGuiCol_TabDimmedSelected] = kBgMed;

    colors[ImGuiCol_DockingPreview] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.45f);
    colors[ImGuiCol_DockingEmptyBg] = kBgDarkest;

    colors[ImGuiCol_PlotLines] = kAccent;
    colors[ImGuiCol_PlotLinesHovered] = kAccentHovered;
    colors[ImGuiCol_PlotHistogram] = kAccent;
    colors[ImGuiCol_PlotHistogramHovered] = kAccentHovered;

    colors[ImGuiCol_TextSelectedBg] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.35f);
    colors[ImGuiCol_DragDropTarget] = kAccent;
    colors[ImGuiCol_NavCursor] = kAccent;
    colors[ImGuiCol_NavWindowingHighlight] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.70f);
    colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.1f, 0.1f, 0.12f, 0.45f);
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.05f, 0.05f, 0.06f, 0.55f);

    // Consistent rounding/spacing everywhere -- one geometry language for
    // every panel instead of each one inheriting ImGui's sharp-cornered
    // defaults untouched.
    style.WindowRounding = 6.0f;
    style.ChildRounding = 4.0f;
    style.FrameRounding = 3.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 8.0f;
    style.GrabRounding = 3.0f;
    style.TabRounding = 4.0f;

    style.WindowPadding = ImVec2(10.0f, 10.0f);
    style.FramePadding = ImVec2(6.0f, 4.0f);
    style.ItemSpacing = ImVec2(8.0f, 6.0f);
    style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
    style.IndentSpacing = 18.0f;
    style.ScrollbarSize = 14.0f;
    style.GrabMinSize = 10.0f;

    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.TabBarBorderSize = 1.0f;

    style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
    style.SeparatorTextBorderSize = 1.0f;
}

void loadKronosFonts(const std::string& fontsDir) {
    ImGuiIO& io = ImGui::GetIO();

    // Real, slightly larger than ImGui's own built-in 13px default --
    // Noto Sans reads more comfortably at this size, and this is the one
    // real, global "readability improvement" every panel gets for free,
    // with zero call sites to touch.
    ImFont* regular = io.Fonts->AddFontFromFileTTF((fontsDir + "/NotoSans-Regular.ttf").c_str(), 17.0f);
    if (regular != nullptr) {
        io.FontDefault = regular;
    } else {
        std::fprintf(stderr, "UITheme: could not load \"%s/NotoSans-Regular.ttf\" -- continuing with ImGui's "
                              "built-in font.\n",
                     fontsDir.c_str());
    }

    g_boldFont = io.Fonts->AddFontFromFileTTF((fontsDir + "/NotoSans-Bold.ttf").c_str(), 18.0f);
    if (g_boldFont == nullptr) {
        std::fprintf(stderr, "UITheme: could not load \"%s/NotoSans-Bold.ttf\" -- section headers/labels fall back "
                              "to the regular weight.\n",
                     fontsDir.c_str());
    }
}

ImFont* kronosBoldFont() { return g_boldFont; }

} // namespace engine::core
