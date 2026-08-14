#pragma once

#include <vector>

#include <glm/glm.hpp>

namespace engine::housedemo {

// Real, static geometry for the house-demo scene (--house-demo, see
// main.cpp). Front door, 2 windows, and the fireplace are handled
// separately by HouseDemoScene.cpp (they need real Door/Interactable/
// Light/ParticleEmitter components a plain box doesn't have) -- this is
// just the wall/roof/floor/furniture boxes.
enum class HousePartKind { Floor, Wall, RoofWedge, FurnitureBlock };

struct HousePart {
    HousePartKind kind = HousePartKind::Wall;
    glm::vec3 localPosition{0.0f}; // house-local space, origin at floor center
    glm::vec3 halfExtents{0.5f};
    glm::vec3 color{0.8f};
    float metallic = 0.0f;
    float roughness = 0.8f;
    float yawDegrees = 0.0f; // only meaningful for RoofWedge (see computeHouseLayout()'s own comment)
};

// Pure, headless, no ECS/Vulkan dependency (glm::vec3 only) -- so this is
// independently unit-testable (engine/tests/test_main.cpp), same
// pure/Vulkan-owning split studio/CreatorToolsSpawning.cpp already
// establishes for CreatorToolsPlugin. HouseDemoScene.cpp is the
// ECS/Vulkan-owning half that spawns real entities from this list.
//
// Footprint: 8m (X) x 6m (Z), wall height 3m, centered on local origin
// at floor level (y=0). Front (south, -Z) wall has a 1.2m door gap
// (HouseDemoScene.cpp spawns the real door there); west wall and back
// (north, +Z) wall each have a window gap (HouseDemoScene.cpp spawns
// the real window panes there) -- this function's own wall segments
// already have those gaps cut out, not overlapping boxes relying on
// later pieces to hide them.
[[nodiscard]] std::vector<HousePart> computeHouseLayout();

} // namespace engine::housedemo
