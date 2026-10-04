#pragma once

#include <string>

struct ImFont;
struct ImDrawData;

namespace engine::core {

struct UIAccent {
    float r, g, b;
};

inline constexpr UIAccent kDefaultUIAccent{0.306f, 0.659f, 0.871f};

// The single Kronos look shared by the player and all four creator apps;
// only the accent differs per app.
void applyKronosUITheme(UIAccent accent = kDefaultUIAccent);

// ImGui authors colors in sRGB; an sRGB swapchain would encode them a second
// time and wash everything out to grey. Call once per frame after
// ImGui::Render() when the target is an sRGB format.
void linearizeDrawDataColors(ImDrawData* drawData);

namespace kronos_palette {
constexpr float kCharcoal[4] = {0.059f, 0.067f, 0.082f, 1.0f};  // #0F1115 app background
constexpr float kSurface[4] = {0.086f, 0.098f, 0.118f, 1.0f};   // #16191E panels
constexpr float kSlate[4] = {0.114f, 0.129f, 0.153f, 1.0f};     // #1D2127 cards
constexpr float kRaised[4] = {0.149f, 0.169f, 0.200f, 1.0f};    // #262B33 controls
constexpr float kSkyBlue[4] = {0.306f, 0.659f, 0.871f, 1.0f};   // #4EA8DE accent
constexpr float kGreen[4] = {0.180f, 0.741f, 0.522f, 1.0f};     // #2EBD85 success / play
constexpr float kGreenHover[4] = {0.255f, 0.808f, 0.592f, 1.0f};
constexpr float kGreenActive[4] = {0.137f, 0.627f, 0.439f, 1.0f};
constexpr float kWarning[4] = {0.910f, 0.663f, 0.231f, 1.0f};   // #E8A93B
constexpr float kDanger[4] = {0.898f, 0.325f, 0.294f, 1.0f};    // #E5534B
constexpr float kTextBright[4] = {0.902f, 0.910f, 0.922f, 1.0f}; // #E6E8EB
constexpr float kTextMuted[4] = {0.608f, 0.631f, 0.667f, 1.0f};  // #9BA1AA
constexpr float kTextFaint[4] = {0.420f, 0.443f, 0.478f, 1.0f};  // #6B717A
constexpr float kBorder[4] = {0.165f, 0.184f, 0.216f, 1.0f};     // #2A2F37
} // namespace kronos_palette

// Inter (Regular/Medium/SemiBold) with Noto Sans merged in as a glyph
// fallback. ImGui 1.92 rasterizes glyphs on demand at the exact on-screen
// pixel size (including the framebuffer scale on high-DPI displays), so
// text stays crisp at any size. Missing files degrade to ImGui's built-in
// font. Call after ImGui::CreateContext(), before the renderer backend init.
void loadKronosFonts(const std::string& fontsDir);

// nullptr when the font failed to load; check before ImGui::PushFont().
[[nodiscard]] ImFont* kronosBoldFont();
[[nodiscard]] ImFont* kronosMediumFont();
[[nodiscard]] ImFont* kronosCodeFont(); // JetBrains Mono, for code editors

} // namespace engine::core
