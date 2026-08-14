#include "housedemo/HouseLayout.hpp"

namespace engine::housedemo {

std::vector<HousePart> computeHouseLayout() {
    std::vector<HousePart> parts;

    const glm::vec3 wallColor{0.85f, 0.83f, 0.78f};
    const glm::vec3 roofColor{0.55f, 0.24f, 0.20f};
    const glm::vec3 floorColor{0.55f, 0.40f, 0.28f};

    // Floor slab -- the terrain provides the outdoor grass, this is the
    // real interior floor sitting on top of it.
    parts.push_back({HousePartKind::Floor, {0.0f, -0.05f, 0.0f}, {4.0f, 0.05f, 3.0f}, floorColor, 0.0f, 0.7f});

    // Front (south, -Z) wall -- 1.2m door gap centered at x=0, plus a
    // lintel closing the gap above the door. HouseDemoScene.cpp spawns
    // the real InteractableDoor into this gap.
    parts.push_back({HousePartKind::Wall, {-2.3f, 1.5f, -3.0f}, {1.7f, 1.5f, 0.1f}, wallColor, 0.0f, 0.8f});
    parts.push_back({HousePartKind::Wall, {2.3f, 1.5f, -3.0f}, {1.7f, 1.5f, 0.1f}, wallColor, 0.0f, 0.8f});
    parts.push_back({HousePartKind::Wall, {0.0f, 2.6f, -3.0f}, {0.6f, 0.4f, 0.1f}, wallColor, 0.0f, 0.8f});

    // Back (north, +Z) wall -- 1.4m window gap centered at x=0 (kitchen
    // window), plus lintel/sill. HouseDemoScene.cpp spawns the real
    // window pane into this gap.
    parts.push_back({HousePartKind::Wall, {-2.35f, 1.5f, 3.0f}, {1.65f, 1.5f, 0.1f}, wallColor, 0.0f, 0.8f});
    parts.push_back({HousePartKind::Wall, {2.35f, 1.5f, 3.0f}, {1.65f, 1.5f, 0.1f}, wallColor, 0.0f, 0.8f});
    parts.push_back({HousePartKind::Wall, {0.0f, 2.6f, 3.0f}, {0.7f, 0.4f, 0.1f}, wallColor, 0.0f, 0.8f});
    parts.push_back({HousePartKind::Wall, {0.0f, 0.5f, 3.0f}, {0.7f, 0.5f, 0.1f}, wallColor, 0.0f, 0.8f});

    // West wall -- 1.4m window gap centered at z=0 (living room window),
    // plus lintel/sill.
    parts.push_back({HousePartKind::Wall, {-4.0f, 1.5f, -1.85f}, {0.1f, 1.5f, 1.15f}, wallColor, 0.0f, 0.8f});
    parts.push_back({HousePartKind::Wall, {-4.0f, 1.5f, 1.85f}, {0.1f, 1.5f, 1.15f}, wallColor, 0.0f, 0.8f});
    parts.push_back({HousePartKind::Wall, {-4.0f, 2.6f, 0.0f}, {0.1f, 0.4f, 0.7f}, wallColor, 0.0f, 0.8f});
    parts.push_back({HousePartKind::Wall, {-4.0f, 0.5f, 0.0f}, {0.1f, 0.5f, 0.7f}, wallColor, 0.0f, 0.8f});

    // East wall -- solid, no gap.
    parts.push_back({HousePartKind::Wall, {4.0f, 1.5f, 0.0f}, {0.1f, 1.5f, 3.0f}, wallColor, 0.0f, 0.8f});

    // Roof -- 2 wedges meeting at a ridge above the wall tops (wall
    // height 3, ridge 4.5). Each wedge's own X-direction end caps (see
    // Mesh::createWedge()'s own left/right triangle faces) double as this
    // house's triangular gable ends -- no separate gable-wall pieces
    // needed. See HouseDemoScene.cpp's own comment on the yaw math that
    // places these two halves correctly (verified by construction, not
    // just asserted -- see engine/tests/test_main.cpp's
    // testHouseLayoutRoofWedgesMeetAtRidge).
    parts.push_back({HousePartKind::RoofWedge, {0.0f, 3.75f, 1.5f}, {4.0f, 0.75f, 1.5f}, roofColor, 0.0f, 0.6f, 0.0f});
    parts.push_back({HousePartKind::RoofWedge, {0.0f, 3.75f, -1.5f}, {4.0f, 0.75f, 1.5f}, roofColor, 0.0f, 0.6f, 180.0f});

    // Kitchen (back-left corner, against the back wall's window).
    parts.push_back(
        {HousePartKind::FurnitureBlock, {-2.5f, 0.45f, 2.6f}, {1.3f, 0.45f, 0.3f}, {0.78f, 0.78f, 0.80f}, 0.1f, 0.5f});
    parts.push_back(
        {HousePartKind::FurnitureBlock, {-1.8f, 0.45f, 2.6f}, {0.4f, 0.45f, 0.3f}, {0.12f, 0.12f, 0.13f}, 0.6f, 0.3f});
    parts.push_back(
        {HousePartKind::FurnitureBlock, {-2.5f, 2.2f, 2.9f}, {1.3f, 0.4f, 0.15f}, {0.55f, 0.38f, 0.22f}, 0.0f, 0.7f});

    return parts;
}

} // namespace engine::housedemo
