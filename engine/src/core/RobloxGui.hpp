#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "core/ECS.hpp"
#include "core/InstanceTree.hpp"

struct ImDrawList;

// Roblox's 2D GUI: the local player's PlayerGui (ScreenGui, Frame, TextLabel,
// TextButton, ImageLabel, UIListLayout, UICorner, UIPadding) laid out on the
// screen, drawn with ImGui, and clickable.
namespace engine::core::gui {

// Mouse in screen pixels, relative to the top-left of the game view.
struct Input {
    glm::vec2 mouse{-1.0f};
    bool pressed = false;  // left button went down this frame
    bool released = false; // left button went up this frame
    bool down = false;
};

struct Item {
    InstanceRef ref = kNoInstance;
    glm::vec2 position{0.0f};
    glm::vec2 size{0.0f};
    glm::vec2 clipMin{0.0f};
    glm::vec2 clipMax{0.0f};
};

// Lays out the GUI for a `screen`-sized view, writes AbsolutePosition and
// AbsoluteSize, and fires MouseEnter/Leave and the button events.
void update(ECS& ecs, glm::vec2 screen, const Input& input);
// What the last update laid out, in drawing order (back to front).
[[nodiscard]] const std::vector<Item>& items(ECS& ecs);
// The mouse is over a visible GUI object that takes clicks.
[[nodiscard]] bool mouseOverButton(ECS& ecs);
// Draws the last layout at `origin` (the game view's top-left corner).
void draw(ECS& ecs, ImDrawList* drawList, glm::vec2 origin);

} // namespace engine::core::gui
