#include "studio/StudioStyle.hpp"

#include <imgui.h>

namespace engine::studio {

namespace {
// Real, simple lerp-toward-white/black -- the same "compute hover/active
// variants from one base accent" approach as every other accent-driven
// control in this file, rather than 4 apps x 3 hand-typed variants each.
ImVec4 lighten(ImVec4 c, float amount) {
    return ImVec4(c.x + (1.0f - c.x) * amount, c.y + (1.0f - c.y) * amount, c.z + (1.0f - c.z) * amount, c.w);
}
ImVec4 darken(ImVec4 c, float amount) {
    return ImVec4(c.x * (1.0f - amount), c.y * (1.0f - amount), c.z * (1.0f - amount), c.w);
}
} // namespace

void applyStudioStyle(StudioAccent accent) {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    // Kronos ("Modern Bespoke UI Theme"): a real, sleek dark-charcoal
    // workspace -- deliberately darker/cooler-neutral than
    // core::applyKronosUITheme()'s own "Warm Ivory & Playful Sunset"
    // light theme (see this function's own header comment on why this is
    // a real fork, not a parameterization of that shared one).
    const ImVec4 kAccent(accent.r, accent.g, accent.b, 1.0f);
    const ImVec4 kAccentHovered = lighten(kAccent, 0.18f);
    const ImVec4 kAccentActive = darken(kAccent, 0.18f);
    const ImVec4 kBgWindow(0.086f, 0.086f, 0.102f, 1.00f); // #16161A
    const ImVec4 kBgCard(0.122f, 0.122f, 0.141f, 1.00f);   // #1F1F24
    // Interactive frames (inputs/combos) sitting inside a card need to
    // read as a distinct sunken surface, so they go slightly darker than
    // the card rather than matching it -- same convention
    // core::applyKronosUITheme() already uses for its own light theme.
    const ImVec4 kBgFrame(0.071f, 0.071f, 0.086f, 1.00f);
    // A low-contrast hairline, not a hard outline -- "subtle panel
    // borders" per the brief, on a dark ground a bright border shouts.
    const ImVec4 kBorder(0.235f, 0.235f, 0.267f, 1.00f);
    const ImVec4 kText(0.93f, 0.93f, 0.95f, 1.00f);
    const ImVec4 kTextDisabled(0.52f, 0.52f, 0.56f, 1.00f);

    colors[ImGuiCol_Text] = kText;
    colors[ImGuiCol_TextDisabled] = kTextDisabled;
    colors[ImGuiCol_WindowBg] = kBgWindow;
    colors[ImGuiCol_ChildBg] = kBgCard;
    colors[ImGuiCol_PopupBg] = ImVec4(kBgCard.x, kBgCard.y, kBgCard.z, 0.98f);
    colors[ImGuiCol_Border] = kBorder;
    colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

    colors[ImGuiCol_FrameBg] = kBgFrame;
    colors[ImGuiCol_FrameBgHovered] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.22f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.35f);

    colors[ImGuiCol_TitleBg] = kBgCard;
    colors[ImGuiCol_TitleBgActive] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.16f);
    colors[ImGuiCol_TitleBgCollapsed] = kBgWindow;
    colors[ImGuiCol_MenuBarBg] = kBgCard;

    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_ScrollbarGrab] = kBorder;
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.55f);
    colors[ImGuiCol_ScrollbarGrabActive] = kAccentActive;

    colors[ImGuiCol_CheckMark] = kAccent;
    colors[ImGuiCol_SliderGrab] = kAccent;
    colors[ImGuiCol_SliderGrabActive] = kAccentActive;

    colors[ImGuiCol_Button] = kBgFrame;
    colors[ImGuiCol_ButtonHovered] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.40f);
    colors[ImGuiCol_ButtonActive] = kAccentActive;

    colors[ImGuiCol_Header] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.20f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.35f);
    colors[ImGuiCol_HeaderActive] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.50f);

    colors[ImGuiCol_Separator] = kBorder;
    colors[ImGuiCol_SeparatorHovered] = kAccentHovered;
    colors[ImGuiCol_SeparatorActive] = kAccentActive;

    colors[ImGuiCol_ResizeGrip] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.20f);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.55f);
    colors[ImGuiCol_ResizeGripActive] = kAccentActive;

    colors[ImGuiCol_Tab] = kBgFrame;
    colors[ImGuiCol_TabHovered] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.50f);
    colors[ImGuiCol_TabSelected] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.28f);
    colors[ImGuiCol_TabSelectedOverline] = kAccent;
    colors[ImGuiCol_TabDimmed] = kBgWindow;
    colors[ImGuiCol_TabDimmedSelected] = kBgFrame;

    colors[ImGuiCol_DockingPreview] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.35f);
    colors[ImGuiCol_DockingEmptyBg] = kBgWindow;

    colors[ImGuiCol_PlotLines] = kAccent;
    colors[ImGuiCol_PlotLinesHovered] = kAccentHovered;
    colors[ImGuiCol_PlotHistogram] = kAccent;
    colors[ImGuiCol_PlotHistogramHovered] = kAccentHovered;

    colors[ImGuiCol_TextSelectedBg] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.32f);
    colors[ImGuiCol_DragDropTarget] = kAccent;
    colors[ImGuiCol_NavCursor] = kAccent;
    colors[ImGuiCol_NavWindowingHighlight] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.70f);
    colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.05f, 0.05f, 0.06f, 0.45f);
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.02f, 0.02f, 0.03f, 0.45f);

    // Consistent rounding/spacing everywhere, refined padding per the
    // brief -- one geometry language for every panel instead of each one
    // inheriting ImGui's sharp-cornered defaults untouched.
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

    // "Subtle panel borders" -- a real, thin 1px hairline (kBorder above
    // is deliberately low-contrast), not the 0px stock ImGui dark theme
    // uses (which reads as no border at all on a charcoal background).
    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    style.TabBarBorderSize = 1.0f;

    style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
    style.SeparatorTextBorderSize = 1.0f;
}

} // namespace engine::studio
