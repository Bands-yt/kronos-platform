#pragma once

#include <string>

struct ImFont;

namespace engine::core {

// Kronos ("UI/UX Revamp"): the one, real, shared Kronos visual identity
// -- palette/rounding/spacing (moved here from studio::applyStudioStyle(),
// which now just calls this) plus, new this pass, a real font (see
// loadKronosFonts() below). Previously Studio-only; engine_runtime's own
// ImGui context (runtime::RuntimeShell) used bare ImGui::StyleColorsDark()
// with no Kronos branding at all -- a real, visible gap between "Studio
// looks like Kronos" and "the Home Screen/Game Catalogue/Session Browser
// look like stock ImGui" this pass closes by giving both real UI
// surfaces the exact same one theme, not two that could drift apart.
void applyKronosUITheme();

// Kronos Client v0.2.0-alpha palette, exposed so the client shell,
// buttons and cards all draw from ONE definition rather than each
// re-typing a hex value that can drift.
namespace kronos_palette {
constexpr float kCharcoal[4] = {0.098f, 0.106f, 0.114f, 1.0f}; // #191B1D background
constexpr float kSlate[4] = {0.137f, 0.145f, 0.153f, 1.0f};    // #232527 cards/containers
constexpr float kSkyBlue[4] = {0.306f, 0.659f, 0.871f, 1.0f};  // #4EA8DE accent / active tab
constexpr float kGreen[4] = {0.000f, 0.698f, 0.349f, 1.0f};    // #00B259 primary action
constexpr float kGreenHover[4] = {0.075f, 0.788f, 0.420f, 1.0f};
constexpr float kGreenActive[4] = {0.000f, 0.588f, 0.290f, 1.0f};
constexpr float kTextBright[4] = {1.0f, 1.0f, 1.0f, 1.0f};     // #FFFFFF
constexpr float kTextMuted[4] = {0.80f, 0.80f, 0.80f, 1.0f};   // #CCCCCC
constexpr float kBorder[4] = {0.204f, 0.216f, 0.227f, 1.0f};
} // namespace kronos_palette

// Kronos ("UI/UX Revamp" -- "bold, thick, modern text"): real,
// redistributable fonts (Noto Sans Regular + Bold, SIL Open Font License
// -- see engine/assets/fonts/ATTRIBUTION.txt), replacing ImGui's own
// built-in bitmap font (ProggyClean, a fixed 13px pixel font with no
// bold weight) everywhere, in both Studio and engine_runtime. Real,
// honest degrade if `fontsDir` doesn't contain the expected files: ImGui
// silently keeps its own built-in font (AddFontDefault() is never
// reached as a fallback here because ImGui already guarantees one exists
// if nothing else is ever added) -- callers don't need to check a return
// value.
//
// Must be called after ImGui::CreateContext() and before the renderer
// backend (ImGui_ImplVulkan_Init()) builds the font atlas texture --
// same real ordering constraint every ImGui backend init already
// documents.
void loadKronosFonts(const std::string& fontsDir);

// The real, bold-weight font loadKronosFonts() loaded, for callers that
// want to set an item label or section header in it (e.g.
// studio::drawPluginHeader(), which every plugin's own real "title"
// treatment already funnels through -- see PluginChrome.hpp). Real,
// honest nullptr if loadKronosFonts() was never called or its bold file
// failed to load -- callers must null-check before ImGui::PushFont()
// (pushing a null font is undefined behavior in Dear ImGui), same
// "check before you use it" contract every other real, optional-load
// resource in this codebase already follows.
[[nodiscard]] ImFont* kronosBoldFont();

} // namespace engine::core
