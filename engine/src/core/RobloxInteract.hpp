#pragma once

#include <string>

#include <glm/glm.hpp>

#include "core/InstanceTree.hpp"

#include "core/ECS.hpp"

// Clicking and hovering parts that hold a ClickDetector. The host calls update
// once a frame with the pointer; this casts a ray through the script camera,
// checks the player is within MaxActivationDistance, fires the events on this
// side and, on a client, tells the server.
namespace engine::core::interact {

struct Pointer {
    glm::vec2 mouse{-1.0f}; // in the game view
    glm::vec2 view{1280.0f, 720.0f};
    bool left = false;      // pressed this frame
    bool right = false;
    bool overGui = false;   // a GUI object or window has the mouse
    float dt = 0.0f;        // seconds since the last update (hold prompts)
};

// A ProximityPrompt the local player can use right now.
struct ShownPrompt {
    InstanceRef ref = kNoInstance;
    glm::vec3 position{0.0f};
    std::string key;        // KeyCode name, "E"
    std::string action;
    std::string object;
    float hold = 0.0f;      // 0..1 while the key is held
};

void update(ECS& ecs, const Pointer& pointer);
// The prompt shown this frame, if any.
[[nodiscard]] const ShownPrompt* shownPrompt(ECS& ecs);
// Draws it next to its part (RobloxInputImGui.cpp, only where ImGui is linked).
// `origin` and `view` are the game view's corner and size in ImGui pixels.
void drawPrompt(ECS& ecs, struct ImDrawList* drawList, glm::vec2 origin, glm::vec2 view);

} // namespace engine::core::interact
