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

// Applies the shared Kronos theme (core::applyKronosUITheme) tinted with
// this app's accent.
void applyStudioStyle(StudioAccent accent);

} // namespace engine::studio
