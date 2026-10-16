#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "core/ECS.hpp"

// UserInputService: keyboard and mouse state for Roblox scripts. The host
// (Player or Studio Play) feeds one Frame per rendered frame; this needs no
// graphics, so tests and headless runs can drive it too.
namespace engine::core::input {

struct KeyEvent {
    std::string key; // Roblox KeyCode name: "W", "Space", "LeftShift", "One"
    bool down = false;
};

struct ButtonEvent {
    int button = 1; // 1 left, 2 right, 3 middle
    bool down = false;
};

struct Frame {
    glm::vec2 mouse{0.0f}; // in the game view, like GetMouseLocation
    glm::vec2 delta{0.0f};
    float wheel = 0.0f; // steps this frame, up is positive
    std::vector<KeyEvent> keys;
    std::vector<ButtonEvent> buttons;
    bool typing = false;       // a text field has the keyboard
    bool overGui = false;      // the mouse is over a GUI object or window
};

// Updates the held state and fires InputBegan/InputEnded/InputChanged.
// Events carry gameProcessed = true when a text box or GUI took the input.
void feed(ECS& ecs, const Frame& frame);

[[nodiscard]] bool isKeyDown(ECS& ecs, const std::string& key);
[[nodiscard]] bool isMouseButtonDown(ECS& ecs, int button);
[[nodiscard]] glm::vec2 mouseLocation(ECS& ecs);
// Mouse movement since the previous frame.
[[nodiscard]] glm::vec2 mouseDelta(ECS& ecs);
[[nodiscard]] std::vector<std::string> keysDown(ECS& ecs);
// A text field had the keyboard in the last frame.
[[nodiscard]] bool typing(ECS& ecs);

// Reads this frame's keys and mouse from ImGui (RobloxInputImGui.cpp, only in
// the binaries that have ImGui) and calls feed. `origin` is the game view's
// top-left corner; `viewHovered` is false when the pointer is over something
// else (a Studio panel); `guiHasMouse` when it is over a Roblox GUI button.
void feedFromImGui(ECS& ecs, glm::vec2 origin, bool viewHovered, bool guiHasMouse);

} // namespace engine::core::input
