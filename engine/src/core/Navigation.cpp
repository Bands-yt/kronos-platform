#include "core/Navigation.hpp"

#include <algorithm>
#include <cmath>

#include "core/Physics.hpp"

namespace engine::core {

const char* navMarkerKindName(NavMarkerKind kind) {
    switch (kind) {
        case NavMarkerKind::Spawn: return "Spawn";
        case NavMarkerKind::Shop: return "Shop";
        case NavMarkerKind::UpgradeKiosk: return "UpgradeKiosk";
        case NavMarkerKind::TeleportPad: return "TeleportPad";
        case NavMarkerKind::Custom: return "Custom";
    }
    return "Custom";
}

std::vector<EntityId> findNavMarkers(ECS& ecs) {
    std::vector<EntityId> result;
    auto view = ecs.view<NavMarker>();
    for (auto entity : view) result.push_back(entity);
    return result;
}

std::vector<EntityId> findNavMarkersOfKind(ECS& ecs, NavMarkerKind kind) {
    std::vector<EntityId> result;
    auto view = ecs.view<NavMarker>();
    for (auto entity : view) {
        if (view.get<NavMarker>(entity).kind == kind) result.push_back(entity);
    }
    return result;
}

glm::vec3 softBoundaryCorrection(glm::vec3 position, const WorldBoundary& boundary, float correctionStrength) {
    glm::vec2 flatOffset(position.x - boundary.center.x, position.z - boundary.center.z);
    float distance = glm::length(flatOffset);
    if (distance <= boundary.softRadius || distance < 0.0001f) return position;

    glm::vec2 direction = flatOffset / distance;
    float excess = distance - boundary.softRadius;
    float clampedDistance = distance - excess * std::clamp(correctionStrength, 0.0f, 1.0f);
    glm::vec2 correctedFlat = direction * clampedDistance;

    return glm::vec3(boundary.center.x + correctedFlat.x, position.y, boundary.center.z + correctedFlat.y);
}

void createWorldBoundaryWalls(ECS& ecs, Physics& physics, const WorldBoundary& boundary, float wallHeight,
                               float wallThickness) {
    float radius = boundary.hardRadius;
    glm::vec3 c = boundary.center;
    float halfHeight = wallHeight * 0.5f;
    float wallY = c.y + halfHeight;

    // North/South walls span X (extended by wallThickness so the
    // perpendicular East/West walls' corners are fully sealed, no gap).
    physics.createStaticBox(ecs, {c.x, wallY, c.z + radius}, {radius + wallThickness, halfHeight, wallThickness});
    physics.createStaticBox(ecs, {c.x, wallY, c.z - radius}, {radius + wallThickness, halfHeight, wallThickness});
    // East/West walls span Z.
    physics.createStaticBox(ecs, {c.x + radius, wallY, c.z}, {wallThickness, halfHeight, radius + wallThickness});
    physics.createStaticBox(ecs, {c.x - radius, wallY, c.z}, {wallThickness, halfHeight, radius + wallThickness});
}

EntityId findLinkedTeleportPad(ECS& ecs, EntityId fromPad) {
    auto* pad = ecs.tryGetComponent<TeleportPad>(fromPad);
    if (pad == nullptr || pad->linkTag.empty()) return kNullEntity;

    auto view = ecs.view<TeleportPad>();
    for (auto entity : view) {
        if (entity == fromPad) continue;
        if (view.get<TeleportPad>(entity).linkTag == pad->linkTag) return entity;
    }
    return kNullEntity;
}

} // namespace engine::core
