#include "core/UITheme.hpp"

#include <array>
#include <cmath>
#include <cstdio>

#include <imgui.h>

namespace engine::core {

namespace {
ImFont* g_boldFont = nullptr;
ImFont* g_mediumFont = nullptr;
ImFont* g_codeFont = nullptr;

ImVec4 rgba(const float (&c)[4], float alpha = 1.0f) { return ImVec4(c[0], c[1], c[2], alpha); }

ImVec4 lighten(ImVec4 c, float amount) {
    return ImVec4(c.x + (1.0f - c.x) * amount, c.y + (1.0f - c.y) * amount, c.z + (1.0f - c.z) * amount, c.w);
}

ImVec4 darken(ImVec4 c, float amount) {
    return ImVec4(c.x * (1.0f - amount), c.y * (1.0f - amount), c.z * (1.0f - amount), c.w);
}

ImFont* addFont(const std::string& path, float size, bool merge) {
    ImFontConfig config;
    config.MergeMode = merge;
    ImFont* font = ImGui::GetIO().Fonts->AddFontFromFileTTF(path.c_str(), size, &config);
    if (font == nullptr) std::fprintf(stderr, "UITheme: could not load \"%s\"\n", path.c_str());
    return font;
}

ImFont* addFamilyFont(const std::string& fontsDir, const char* primary, float size) {
    ImFont* font = addFont(fontsDir + "/" + primary, size, false);
    if (font == nullptr) return nullptr;
    addFont(fontsDir + "/NotoSans-Regular.ttf", size, true);
    return font;
}
} // namespace

void applyKronosUITheme(UIAccent accentRgb) {
    using namespace kronos_palette;
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    const ImVec4 accent(accentRgb.r, accentRgb.g, accentRgb.b, 1.0f);
    const ImVec4 accentHover = lighten(accent, 0.15f);
    const ImVec4 accentActive = darken(accent, 0.15f);
    const ImVec4 bg0 = rgba(kCharcoal);
    const ImVec4 bg1 = rgba(kSurface);
    const ImVec4 bg2 = rgba(kSlate);
    const ImVec4 bg3 = rgba(kRaised);
    const ImVec4 bg4 = lighten(bg3, 0.06f);
    const ImVec4 border = rgba(kBorder);
    auto accentA = [&](float a) { return ImVec4(accent.x, accent.y, accent.z, a); };

    colors[ImGuiCol_Text] = rgba(kTextBright);
    colors[ImGuiCol_TextDisabled] = rgba(kTextMuted);
    colors[ImGuiCol_WindowBg] = bg1;
    colors[ImGuiCol_ChildBg] = bg2;
    colors[ImGuiCol_PopupBg] = ImVec4(bg2.x, bg2.y, bg2.z, 0.985f);
    colors[ImGuiCol_Border] = border;
    colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

    colors[ImGuiCol_FrameBg] = bg3;
    colors[ImGuiCol_FrameBgHovered] = bg4;
    colors[ImGuiCol_FrameBgActive] = lighten(bg4, 0.04f);

    colors[ImGuiCol_TitleBg] = bg0;
    colors[ImGuiCol_TitleBgActive] = bg0;
    colors[ImGuiCol_TitleBgCollapsed] = bg0;
    colors[ImGuiCol_MenuBarBg] = bg0;

    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(1.0f, 1.0f, 1.0f, 0.10f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(1.0f, 1.0f, 1.0f, 0.18f);
    colors[ImGuiCol_ScrollbarGrabActive] = accentA(0.75f);

    colors[ImGuiCol_CheckMark] = accent;
    colors[ImGuiCol_SliderGrab] = accent;
    colors[ImGuiCol_SliderGrabActive] = accentHover;

    colors[ImGuiCol_Button] = bg3;
    colors[ImGuiCol_ButtonHovered] = bg4;
    colors[ImGuiCol_ButtonActive] = accentA(0.45f);

    colors[ImGuiCol_Header] = accentA(0.20f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(1.0f, 1.0f, 1.0f, 0.06f);
    colors[ImGuiCol_HeaderActive] = accentA(0.30f);

    colors[ImGuiCol_Separator] = border;
    colors[ImGuiCol_SeparatorHovered] = accentA(0.70f);
    colors[ImGuiCol_SeparatorActive] = accent;

    colors[ImGuiCol_ResizeGrip] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_ResizeGripHovered] = accentA(0.55f);
    colors[ImGuiCol_ResizeGripActive] = accentActive;

    colors[ImGuiCol_InputTextCursor] = accent;
    colors[ImGuiCol_Tab] = bg0;
    colors[ImGuiCol_TabHovered] = bg3;
    colors[ImGuiCol_TabSelected] = bg1;
    colors[ImGuiCol_TabSelectedOverline] = accent;
    colors[ImGuiCol_TabDimmed] = bg0;
    colors[ImGuiCol_TabDimmedSelected] = bg1;
    colors[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

    colors[ImGuiCol_DockingPreview] = accentA(0.30f);
    colors[ImGuiCol_DockingEmptyBg] = bg0;

    colors[ImGuiCol_PlotLines] = accent;
    colors[ImGuiCol_PlotLinesHovered] = accentHover;
    colors[ImGuiCol_PlotHistogram] = accent;
    colors[ImGuiCol_PlotHistogramHovered] = accentHover;

    colors[ImGuiCol_TableHeaderBg] = bg2;
    colors[ImGuiCol_TableBorderStrong] = border;
    colors[ImGuiCol_TableBorderLight] = ImVec4(border.x, border.y, border.z, 0.6f);
    colors[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 1.0f, 1.0f, 0.02f);

    colors[ImGuiCol_TextLink] = accentHover;
    colors[ImGuiCol_TextSelectedBg] = accentA(0.30f);
    colors[ImGuiCol_DragDropTarget] = accent;
    colors[ImGuiCol_NavCursor] = accent;
    colors[ImGuiCol_NavWindowingHighlight] = accentA(0.70f);
    colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.45f);
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.55f);

    style.WindowRounding = 8.0f;
    style.ChildRounding = 8.0f;
    style.FrameRounding = 6.0f;
    style.PopupRounding = 8.0f;
    style.ScrollbarRounding = 12.0f;
    style.GrabRounding = 6.0f;
    style.TabRounding = 6.0f;

    style.WindowPadding = ImVec2(12.0f, 12.0f);
    style.FramePadding = ImVec2(9.0f, 5.0f);
    style.CellPadding = ImVec2(8.0f, 5.0f);
    style.ItemSpacing = ImVec2(8.0f, 7.0f);
    style.ItemInnerSpacing = ImVec2(6.0f, 5.0f);
    style.IndentSpacing = 16.0f;
    style.ScrollbarSize = 11.0f;
    style.GrabMinSize = 10.0f;

    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.TabBorderSize = 0.0f;
    style.TabBarBorderSize = 1.0f;
    style.TabBarOverlineSize = 2.0f;
    style.DockingSeparatorSize = 2.0f;
    style.SeparatorTextBorderSize = 1.0f;

    style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
    style.WindowMenuButtonPosition = ImGuiDir_None;
    style.CircleTessellationMaxError = 0.18f;
}

void loadKronosFonts(const std::string& fontsDir) {
    ImGuiIO& io = ImGui::GetIO();
    constexpr float kBaseSize = 15.0f;
    ImFont* regular = addFamilyFont(fontsDir, "Inter-Regular.ttf", kBaseSize);
    if (regular == nullptr) regular = addFont(fontsDir + "/NotoSans-Regular.ttf", kBaseSize + 1.0f, false);
    if (regular != nullptr) {
        io.FontDefault = regular;
        ImGui::GetStyle().FontSizeBase = kBaseSize;
    }
    g_mediumFont = addFamilyFont(fontsDir, "Inter-Medium.ttf", kBaseSize);
    g_boldFont = addFamilyFont(fontsDir, "Inter-SemiBold.ttf", kBaseSize);
    if (g_boldFont == nullptr) g_boldFont = addFont(fontsDir + "/NotoSans-Bold.ttf", kBaseSize + 1.0f, false);
    if (g_mediumFont == nullptr) g_mediumFont = g_boldFont;
    g_codeFont = addFont(fontsDir + "/JetBrainsMono-Regular.ttf", kBaseSize, false);
}

ImFont* kronosBoldFont() { return g_boldFont; }
ImFont* kronosMediumFont() { return g_mediumFont; }
ImFont* kronosCodeFont() { return g_codeFont; }

void linearizeDrawDataColors(ImDrawData* drawData) {
    static const std::array<unsigned char, 256> kToLinear = [] {
        std::array<unsigned char, 256> table{};
        for (int i = 0; i < 256; ++i) {
            float c = static_cast<float>(i) / 255.0f;
            float linear = c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
            table[i] = static_cast<unsigned char>(std::lround(linear * 255.0f));
        }
        return table;
    }();
    if (drawData == nullptr) return;
    for (ImDrawList* list : drawData->CmdLists) {
        for (ImDrawVert& vertex : list->VtxBuffer) {
            ImU32 c = vertex.col;
            ImU32 r = kToLinear[(c >> IM_COL32_R_SHIFT) & 0xFF];
            ImU32 g = kToLinear[(c >> IM_COL32_G_SHIFT) & 0xFF];
            ImU32 b = kToLinear[(c >> IM_COL32_B_SHIFT) & 0xFF];
            vertex.col = (c & IM_COL32_A_MASK) | (r << IM_COL32_R_SHIFT) | (g << IM_COL32_G_SHIFT) |
                         (b << IM_COL32_B_SHIFT);
        }
    }
}

} // namespace engine::core
