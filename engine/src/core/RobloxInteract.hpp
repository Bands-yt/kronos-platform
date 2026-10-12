#pragma once

#include <glm/glm.hpp>

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
};

void update(ECS& ecs, const Pointer& pointer);

} // namespace engine::core::interact
