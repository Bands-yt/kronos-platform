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

    // Kronos ("Warm Ivory & Playful Sunset"): real, explicit palette --
    // a full light-theme replacement for the previous dark
    // charcoal/teal look, applied through this same shared function
    // (engine_runtime's Home/Settings/Shop/etc. panels and every real
    // Studio panel alike -- see this function's own long-standing "one
    // real, shared ImGui style, not two independently-drifting themes"
    // precedent). Coral/orange was chosen specifically because it sits
    // far from every ImGuizmo axis color (red/green/blue), the same
    // real "don't collide with the gizmo" reasoning that ruled out a
    // green accent -- so pushPrimaryActionButtonColors() (RuntimeShell.cpp)
    // was updated to this same coral rather than left on its old,
    // now-inconsistent green.
    const ImVec4 kAccent(0.867f, 0.420f, 0.125f, 1.00f);         // #DD6B20
    const ImVec4 kAccentHovered(0.918f, 0.494f, 0.204f, 1.00f);  // brighter, ~#EA7E34
    const ImVec4 kAccentActive(0.729f, 0.325f, 0.086f, 1.00f);   // darker, ~#BA5316
    const ImVec4 kBgWindow(0.980f, 0.980f, 0.973f, 1.00f);       // #FAFAF8 warm ivory
    const ImVec4 kBgCard(1.00f, 1.00f, 1.00f, 1.00f);            // #FFFFFF -- panels/cards/child windows
    // A light neutral distinct from both kBgWindow and kBgCard, for
    // interactive frames (inputs/sliders/combos) sitting *inside* a
    // white card -- pure white-on-white would read as one flat surface
    // with no visible field boundary.
    const ImVec4 kBgFrame(0.953f, 0.949f, 0.937f, 1.00f);
    const ImVec4 kBorder(0.886f, 0.910f, 0.941f, 1.00f);         // #E2E8F0 soft warm gray
    const ImVec4 kText(0.176f, 0.216f, 0.282f, 1.00f);           // #2D3748
    const ImVec4 kTextDisabled(0.60f, 0.62f, 0.65f, 1.00f);

    colors[ImGuiCol_Text] = kText;
    colors[ImGuiCol_TextDisabled] = kTextDisabled;
    colors[ImGuiCol_WindowBg] = kBgWindow;
    // Kronos ("Warm Ivory & Playful Sunset"): real, opaque white --
    // previously a subtle dark-tinted translucent overlay (a dark-theme
    // technique); a light theme's own "distinct region" cue is a real,
    // fully-opaque white card against the ivory window behind it, the
    // literal "Panels/Cards/Child Windows: Pure white" spec, not a
    // reskin of the old alpha-overlay approach.
    colors[ImGuiCol_ChildBg] = kBgCard;
    colors[ImGuiCol_PopupBg] = ImVec4(kBgCard.x, kBgCard.y, kBgCard.z, 0.98f);
    colors[ImGuiCol_Border] = kBorder;
    colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

    colors[ImGuiCol_FrameBg] = kBgFrame;
    colors[ImGuiCol_FrameBgHovered] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.18f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.30f);

    colors[ImGuiCol_TitleBg] = kBgCard;
    colors[ImGuiCol_TitleBgActive] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.12f);
    colors[ImGuiCol_TitleBgCollapsed] = kBgWindow;
    colors[ImGuiCol_MenuBarBg] = kBgCard;

    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_ScrollbarGrab] = kBorder;
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.55f);
    colors[ImGuiCol_ScrollbarGrabActive] = kAccentActive;

    colors[ImGuiCol_CheckMark] = kAccent;
    colors[ImGuiCol_SliderGrab] = kAccent;
    colors[ImGuiCol_SliderGrabActive] = kAccentActive;

    // Kronos ("signature accent... for... primary buttons"): real --
    // ordinary buttons get a light neutral (they're not all "primary"),
    // but hovered/active states use the real coral accent so every
    // button still reads as genuinely interactive; real primary-action
    // buttons (pushPrimaryActionButtonColors()) use full-strength
    // kAccent for their own resting state on top of this.
    colors[ImGuiCol_Button] = kBgFrame;
    colors[ImGuiCol_ButtonHovered] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.35f);
    colors[ImGuiCol_ButtonActive] = kAccentActive;

    colors[ImGuiCol_Header] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.16f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.30f);
    colors[ImGuiCol_HeaderActive] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.45f);

    colors[ImGuiCol_Separator] = kBorder;
    colors[ImGuiCol_SeparatorHovered] = kAccentHovered;
    colors[ImGuiCol_SeparatorActive] = kAccentActive;

    colors[ImGuiCol_ResizeGrip] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.20f);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.55f);
    colors[ImGuiCol_ResizeGripActive] = kAccentActive;

    colors[ImGuiCol_Tab] = kBgFrame;
    colors[ImGuiCol_TabHovered] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.45f);
    colors[ImGuiCol_TabSelected] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.22f);
    colors[ImGuiCol_TabSelectedOverline] = kAccent;
    colors[ImGuiCol_TabDimmed] = kBgWindow;
    colors[ImGuiCol_TabDimmedSelected] = kBgFrame;

    colors[ImGuiCol_DockingPreview] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.35f);
    colors[ImGuiCol_DockingEmptyBg] = kBgWindow;

    colors[ImGuiCol_PlotLines] = kAccent;
    colors[ImGuiCol_PlotLinesHovered] = kAccentHovered;
    colors[ImGuiCol_PlotHistogram] = kAccent;
    colors[ImGuiCol_PlotHistogramHovered] = kAccentHovered;

    colors[ImGuiCol_TextSelectedBg] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.28f);
    colors[ImGuiCol_DragDropTarget] = kAccent;
    colors[ImGuiCol_NavCursor] = kAccent;
    colors[ImGuiCol_NavWindowingHighlight] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.70f);
    colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.9f, 0.9f, 0.88f, 0.45f);
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.3f, 0.28f, 0.24f, 0.35f);

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
    // Kronos ("Warm Ivory & Playful Sunset" -- "soft warm gray
    // borders"): real, was 0 under the old dark theme (FrameBg's own
    // color contrast alone read fine there); a light theme's white-
    // card-on-ivory-window layout needs a visible 1px frame border so
    // an input field doesn't visually disappear into its own card.
    style.FrameBorderSize = 1.0f;
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
