#pragma once

namespace engine::studio {

// Kronos ("Modern Bespoke UI Theme"): the one real accent color each of
// the 4 standalone apps tints every interactive element with (buttons,
// active tabs, active headers, slider grabs, checkmarks, ...) on top of
// the shared dark-charcoal workspace base applyStudioStyle() below always
// applies. A plain 3-float struct (not engine::studio::StudioApp::
// StudioMode) so this header never has to include StudioApp.hpp (which
// itself pulls in ~40 other headers and is StudioStyle.hpp's own
// consumer) -- StudioApp.cpp does the real mode -> accent mapping, right
// next to its own brandName() switch, and just hands the 3 floats here.
struct StudioAccent {
    float r, g, b;
};

// Applies Studio's own visual identity on top of ImGui::StyleColorsDark()
// (called once, right after it, in StudioApp::initImGuiVulkanBackend()) --
// consistent rounding/spacing across every panel, plus (Kronos "Modern
// Bespoke UI Theme") a real, sleek dark-charcoal workspace palette
// (#16161a window / #1f1f24 panel background, subtle borders, refined
// padding) instead of stock ImGui::StyleColorsDark()'s own grays, with
// `accent` tinting every interactive element (instead of each panel
// picking its own ad hoc ImGui::PushStyleColor() calls -- MaterialPlugin's
// preset swatches are a deliberate, stated exception -- see its own
// comment -- everything else in Studio goes through this one palette).
// Pure ImGui::GetStyle() mutation, no fonts/textures touched -- see
// StudioIcons.hpp for the separate, vector-drawn icon system this
// pairs with instead of an icon font (see that header's comment on why).
//
// Deliberately independent of core::applyKronosUITheme() (UITheme.hpp) --
// that one real, shared theme is what engine_runtime's own
// runtime::RuntimeShell (the player-facing game client) still uses
// unchanged; forking Studio's own theme here rather than parameterizing
// that shared function keeps this pass's real scope (the 4 creator-tool
// apps only) from also silently reskinning the game client nobody asked
// to change.
void applyStudioStyle(StudioAccent accent);

} // namespace engine::studio
