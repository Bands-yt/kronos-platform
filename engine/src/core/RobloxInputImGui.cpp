#include "core/RobloxInput.hpp"

#include <imgui.h>

namespace engine::core::input {

namespace {

struct KeyName {
    ImGuiKey key;
    const char* name;
};

const KeyName kKeys[] = {
    {ImGuiKey_A, "A"}, {ImGuiKey_B, "B"}, {ImGuiKey_C, "C"}, {ImGuiKey_D, "D"}, {ImGuiKey_E, "E"}, {ImGuiKey_F, "F"},
    {ImGuiKey_G, "G"}, {ImGuiKey_H, "H"}, {ImGuiKey_I, "I"}, {ImGuiKey_J, "J"}, {ImGuiKey_K, "K"}, {ImGuiKey_L, "L"},
    {ImGuiKey_M, "M"}, {ImGuiKey_N, "N"}, {ImGuiKey_O, "O"}, {ImGuiKey_P, "P"}, {ImGuiKey_Q, "Q"}, {ImGuiKey_R, "R"},
    {ImGuiKey_S, "S"}, {ImGuiKey_T, "T"}, {ImGuiKey_U, "U"}, {ImGuiKey_V, "V"}, {ImGuiKey_W, "W"}, {ImGuiKey_X, "X"},
    {ImGuiKey_Y, "Y"}, {ImGuiKey_Z, "Z"},
    {ImGuiKey_0, "Zero"}, {ImGuiKey_1, "One"}, {ImGuiKey_2, "Two"}, {ImGuiKey_3, "Three"}, {ImGuiKey_4, "Four"},
    {ImGuiKey_5, "Five"}, {ImGuiKey_6, "Six"}, {ImGuiKey_7, "Seven"}, {ImGuiKey_8, "Eight"}, {ImGuiKey_9, "Nine"},
    {ImGuiKey_Space, "Space"}, {ImGuiKey_Enter, "Return"}, {ImGuiKey_Escape, "Escape"}, {ImGuiKey_Tab, "Tab"},
    {ImGuiKey_Backspace, "Backspace"}, {ImGuiKey_Delete, "Delete"}, {ImGuiKey_Insert, "Insert"},
    {ImGuiKey_Home, "Home"}, {ImGuiKey_End, "End"}, {ImGuiKey_PageUp, "PageUp"}, {ImGuiKey_PageDown, "PageDown"},
    {ImGuiKey_UpArrow, "Up"}, {ImGuiKey_DownArrow, "Down"}, {ImGuiKey_LeftArrow, "Left"}, {ImGuiKey_RightArrow, "Right"},
    {ImGuiKey_LeftShift, "LeftShift"}, {ImGuiKey_RightShift, "RightShift"},
    {ImGuiKey_LeftCtrl, "LeftControl"}, {ImGuiKey_RightCtrl, "RightControl"},
    {ImGuiKey_LeftAlt, "LeftAlt"}, {ImGuiKey_RightAlt, "RightAlt"},
    {ImGuiKey_F1, "F1"}, {ImGuiKey_F2, "F2"}, {ImGuiKey_F3, "F3"}, {ImGuiKey_F4, "F4"}, {ImGuiKey_F5, "F5"},
    {ImGuiKey_F6, "F6"}, {ImGuiKey_F7, "F7"}, {ImGuiKey_F8, "F8"}, {ImGuiKey_F9, "F9"}, {ImGuiKey_F10, "F10"},
    {ImGuiKey_F11, "F11"}, {ImGuiKey_F12, "F12"},
    {ImGuiKey_Minus, "Minus"}, {ImGuiKey_Equal, "Equals"}, {ImGuiKey_Comma, "Comma"}, {ImGuiKey_Period, "Period"},
    {ImGuiKey_Slash, "Slash"}, {ImGuiKey_Semicolon, "Semicolon"}, {ImGuiKey_Apostrophe, "Quote"},
    {ImGuiKey_LeftBracket, "LeftBracket"}, {ImGuiKey_RightBracket, "RightBracket"},
    {ImGuiKey_Backslash, "BackSlash"}, {ImGuiKey_GraveAccent, "Backquote"},
};

} // namespace

void feedFromImGui(ECS& ecs, glm::vec2 origin, bool viewHovered, bool guiHasMouse) {
    const ImGuiIO& io = ImGui::GetIO();
    Frame frame;
    frame.mouse = glm::vec2(io.MousePos.x, io.MousePos.y) - origin;
    frame.delta = glm::vec2(io.MouseDelta.x, io.MouseDelta.y);
    frame.typing = io.WantTextInput;
    frame.overGui = guiHasMouse || !viewHovered;
    frame.wheel = viewHovered ? io.MouseWheel : 0.0f;
    // A press that started outside the game view isn't the game's.
    for (const KeyName& k : kKeys) {
        if (ImGui::IsKeyPressed(k.key, false)) {
            if (viewHovered || io.WantCaptureKeyboard == false) frame.keys.push_back({k.name, true});
        } else if (ImGui::IsKeyReleased(k.key)) {
            frame.keys.push_back({k.name, false});
        }
    }
    for (int b = 0; b < 3; ++b) {
        const int button = b == 0 ? 1 : b == 1 ? 2 : 3;
        if (ImGui::IsMouseClicked(b) && viewHovered) frame.buttons.push_back({button, true});
        if (ImGui::IsMouseReleased(b)) frame.buttons.push_back({button, false});
    }
    feed(ecs, frame);
}

} // namespace engine::core::input
