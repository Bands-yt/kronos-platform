#pragma once

#include <imgui.h>

// Kronos's own widget set on top of ImGui: eased hover/press transitions,
// soft elevation shadows and accent glows. State lives in ImGui's per-window
// storage, so every widget is immediate-mode like the stock ones.
namespace engine::ui {

enum class ButtonKind { Primary, Secondary, Ghost, Danger, Success };

enum class Icon { None, Home, Discover, Avatar, People, Create, Settings, Bell, Search, Play, Download, Check, Chat };

// Stroke-drawn vector icon centred in a `size` x `size` box.
void drawIcon(ImDrawList* drawList, Icon icon, ImVec2 center, float size, ImU32 color);

// Eases a per-id value toward `target`; `speed` is roughly 1 / time-constant.
float animate(ImGuiID id, float target, float speed = 14.0f);

ImVec4 accent();
ImU32 mix(ImVec4 a, ImVec4 b, float t, float alphaScale = 1.0f);

void softShadow(ImDrawList* drawList, ImVec2 min, ImVec2 max, float rounding, float size, ImVec4 color);

bool button(const char* label, ButtonKind kind = ButtonKind::Secondary, ImVec2 size = ImVec2(0.0f, 0.0f));
// Sidebar navigation row: animated accent pill, optional muted subtitle.
bool navItem(const char* label, bool selected, const char* subtitle = nullptr, Icon icon = Icon::None);
// Square ghost button holding an icon; `dot` adds an accent badge.
bool iconButton(const char* id, Icon icon, float size, bool dot = false);
bool toggle(const char* label, bool* value);
// Segmented control; returns true when the selection changed.
bool segmented(const char* id, const char* const* labels, int count, int* selected);

// Auto-height card with an elevation shadow. Always pair with endCard().
void beginCard(const char* id, ImVec2 padding = ImVec2(16.0f, 14.0f));
void endCard();

void sectionHeader(const char* text);
void pageTitle(const char* title, const char* subtitle = nullptr);
// Left-column label (+ optional muted description) for a settings-style
// row; sizes and positions the next item as the right-column control.
void settingLabel(const char* label, const char* description = nullptr);
void badge(const char* text, ImVec4 color);

} // namespace engine::ui
