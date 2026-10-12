#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "core/ECS.hpp"
#include "core/InstanceTree.hpp"

struct ImDrawList;

// Roblox's 2D GUI: the local player's PlayerGui laid out on the screen,
// drawn with ImGui, and interactive. Layout and input (RobloxGuiLayout.cpp)
// need no graphics, so headless runs get AbsolutePosition too; drawing is
// in RobloxGui.cpp.
namespace engine::core::gui {

// Mouse in screen pixels, relative to the top-left of the game view.
struct Input {
    glm::vec2 mouse{-1.0f};
    bool pressed = false;  // left button went down this frame
    bool released = false; // left button went up this frame
    bool down = false;
    float wheel = 0.0f; // mouse wheel steps this frame, up is positive
};

struct Item {
    InstanceRef ref = kNoInstance;
    glm::vec2 position{0.0f};
    glm::vec2 size{0.0f};
    glm::vec2 clipMin{0.0f};
    glm::vec2 clipMax{0.0f};
    float scale = 1.0f; // from UIScale on this object or an ancestor
};

// Lays out the GUI for a `screen`-sized view, writes the Absolute*
// outputs, scrolls ScrollingFrames, and fires MouseEnter/Leave, the
// button events and TextBox focus changes.
void update(ECS& ecs, glm::vec2 screen, const Input& input);
// What the last update laid out, in drawing order (back to front).
[[nodiscard]] const std::vector<Item>& items(ECS& ecs);
// The mouse is over a visible GUI object that takes clicks or text.
[[nodiscard]] bool mouseOverButton(ECS& ecs);
[[nodiscard]] InstanceRef pressedButton(ECS& ecs);
[[nodiscard]] InstanceRef hoveredButton(ECS& ecs);

// TextBox focus: TextBox:CaptureFocus/ReleaseFocus, or a click.
[[nodiscard]] InstanceRef focusedTextBox(ECS& ecs);
void captureFocus(ECS& ecs, InstanceRef box);
// Fires FocusLost(submitted) when `box` had the focus.
void releaseFocus(ECS& ecs, InstanceRef box, bool submitted);

// Images: maps an Image property ("rbxassetid://1", "images/coin.png") to
// something the host can draw, or nullptr when it can't. Set by the host.
using ImageResolver = std::function<void*(const std::string& image)>;
void setImageResolver(ECS& ecs, ImageResolver resolver);
[[nodiscard]] void* resolveImage(ECS& ecs, const std::string& image);

// The file an Image property points at, or "" when there's none.
// "rbxassetid://123" (or a roblox.com/asset?id=123 link) looks in each root's
// asset_map.json / assets/asset_map.json ({"123": "images/coin.png"}), then
// for assets/123.png, assets/images/123.png, images/123.png and 123.png
// (.jpg/.jpeg too). Other text is a path relative to a root and must stay
// inside it. Roblox's own rbxasset:// files and web links are never fetched.
[[nodiscard]] std::string imageFile(const std::string& image, const std::vector<std::filesystem::path>& roots);
[[nodiscard]] std::string assetIdOf(const std::string& image);
// Image properties in the place that imageFile can't find, sorted.
[[nodiscard]] std::vector<std::string> missingImages(ECS& ecs, const std::vector<std::filesystem::path>& roots);

// Draws the last layout at `origin` (the game view's top-left corner).
// A focused TextBox is edited through an ImGui text field over it.
void draw(ECS& ecs, ImDrawList* drawList, glm::vec2 origin);

} // namespace engine::core::gui
