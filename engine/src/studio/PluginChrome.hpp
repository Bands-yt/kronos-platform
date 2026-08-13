#pragma once

namespace engine::studio {

// Sprint 7 ("Studio UI Revamp") task category 5's "Plugin Chrome": a
// small, reusable header/footer any plugin's drawPanel() can call right
// after ImGui::Begin() / right before ImGui::End(), so plugin windows
// share one consistent accent-colored title treatment and status footer
// instead of each one either inventing its own or (the common case
// today) having none at all -- just ImGui's stock title bar with content
// starting immediately underneath it. Deliberately two free functions,
// not a base class or template-method hook IStudioPlugin has to opt
// into -- see that header's own comment on why drawPanel() stays a
// plain virtual with no structure imposed on it; applying chrome is the
// plugin's own choice, made explicitly at its own call sites.
//
// Applied to CreatorToolsPlugin (new) plus a representative sample of
// existing plugins spanning different categories (TerrainEditorPlugin/
// World, MaterialPlugin/Utility, ShopPlugin/Economy) -- not swept across
// every plugin file mechanically, since most first-party plugins predate
// this pass and retrofitting all of them is out of this task's stated
// scope ("new + representative" plugins, not "every" plugin).

// Draws `title` in Studio's own accent color (read back from
// ImGuiCol_CheckMark, the single color StudioStyle.cpp already bakes
// every interactive/accent element from -- see this .cpp's own comment
// for why that's the real source of truth rather than a second hardcoded
// copy of the RGB triple) plus a thin accent-colored rule beneath it.
// Call once, immediately after ImGui::Begin().
void drawPluginHeader(const char* title);

// Draws a thin separator plus `statusText` in disabled-text styling
// beneath it (the text is skipped entirely when `statusText` is empty,
// so a plugin with nothing to report still gets the visual separation
// from the window's bottom edge). Call once, immediately before
// ImGui::End().
void drawPluginFooter(const char* statusText = "");

// Sprint 10 ("Creator Tools Phase 2") task category 5's "tooltips and
// help text for all creator tools" -- a real, reusable "(?)" marker
// (the standard Dear ImGui "HelpMarker" idiom) that shows `text` in a
// real hover tooltip. Call immediately after the control it explains
// (typically via ImGui::SameLine() first) rather than replacing that
// control's own label -- this is for the *why*/nuance a label alone
// doesn't convey (e.g. why a slider's default is what it is), not a
// restatement of what the control obviously does.
void helpMarker(const char* text);

} // namespace engine::studio
