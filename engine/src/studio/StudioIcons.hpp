#pragma once

#include <imgui.h>

namespace engine::studio {

// Small, dependency-free vector icons drawn directly with ImDrawList
// primitives -- deliberately not an icon font (Font Awesome/Codicons/
// etc): pulling one in means a FetchContent URL/checksum this codebase
// has no way to verify keeps resolving to the same asset forever
// (unlike the git-tag-pinned dependencies in cmake/Dependencies.cmake),
// plus glyph-range/codepoint-merge plumbing, for a handful of buttons in
// one toolbar. The original six primitives (Translate through Snap) were
// drawn for ViewportPanel's gizmo toolbar; Sprint 7 ("Studio UI Revamp")
// added seven more (Terrain through Folder) for the Explorer hierarchy,
// the Creator Tools panel, and Inspector section headers -- same
// technique, same "follows ImGui::GetStyle() colors automatically"
// property, just more subjects.
enum class Icon {
    Translate,
    Rotate,
    Scale,
    WorldSpace,
    LocalSpace,
    Snap,
    Terrain,
    Prop,
    Script,
    Material,
    Physics,
    Lighting,
    Folder,
};

// Draws `icon` centered at `center`, sized to fit within a `size` x
// `size` box, in `color`. Pure drawing -- no ImGui item/layout state.
void drawIcon(ImDrawList* drawList, Icon icon, ImVec2 center, float size, ImU32 color);

// A square toolbar button drawn with drawIcon() instead of a text label.
// Reuses ImGuiCol_Header*/FrameBg for its background so it visually
// reads as "part of the same widget family" as every other themed
// control rather than a bespoke one-off. Returns true the frame it's
// clicked; shows `tooltip` on hover if non-null. Must be called with an
// ImGui window already begun, same as any other ImGui widget call.
bool iconButton(const char* strId, Icon icon, ImVec2 buttonSize, bool active, const char* tooltip = nullptr);

// Sprint 7: real, distinct color-coding per icon category (task
// category 2's "color-coded categories for clarity") -- a small, pure,
// headlessly-testable lookup (no ImGui types involved, unlike drawIcon()/
// iconButton() above) returning a plain RGB triple a caller multiplies
// into whatever alpha/theme context it's drawing in. Deliberately
// separate hues from StudioStyle's own teal/cyan accent (see that
// file's header comment) -- a category color communicates "what kind of
// thing is this", not "this is interactive", the same semantic-vs-accent
// separation Notification.hpp's severity colors already establish.
struct IconCategoryColor {
    float r, g, b;
};
[[nodiscard]] IconCategoryColor iconCategoryColor(Icon icon);

// A short, human-readable category name per icon -- what the Explorer's
// grouped hierarchy (task category 3's "folders") shows as each group's
// header label, and what a tooltip/Inspector section title reads.
[[nodiscard]] const char* iconCategoryName(Icon icon);

} // namespace engine::studio
