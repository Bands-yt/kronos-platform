#include "core/Interactable.hpp"

#include <algorithm>

#include "core/Components.hpp"

namespace engine::core {

bool canInteract(const Interactable& interactable, float currentTimeSeconds) {
    if (interactable.cooldownSeconds <= 0.0f) return true;
    return currentTimeSeconds - interactable.lastInteractedAtSeconds >= interactable.cooldownSeconds;
}

void markInteracted(Interactable& interactable, float currentTimeSeconds) {
    interactable.lastInteractedAtSeconds = currentTimeSeconds;
}

std::vector<EntityId> findInteractablesInRange(ECS& ecs, glm::vec3 origin) {
    std::vector<std::pair<float, EntityId>> withDistance;

    auto view = ecs.view<Interactable, Transform>();
    for (auto entity : view) {
        auto& interactable = view.get<Interactable>(entity);
        if (!interactable.proximityEnabled) continue;

        auto& transform = view.get<Transform>(entity);
        float distance = glm::length(transform.position - origin);
        if (distance <= interactable.proximityRadius) {
            withDistance.emplace_back(distance, entity);
        }
    }

    std::sort(withDistance.begin(), withDistance.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    std::vector<EntityId> result;
    result.reserve(withDistance.size());
    for (const auto& [distance, entity] : withDistance) result.push_back(entity);
    return result;
}

EntityId resolveInteractionTarget(EntityId lookAtTarget, EntityId nearestProximityTarget, ECS& ecs,
                                   float currentTimeSeconds) {
    EntityId target = lookAtTarget != kNullEntity ? lookAtTarget : nearestProximityTarget;
    if (target == kNullEntity) return kNullEntity;

    auto* interactable = ecs.tryGetComponent<Interactable>(target);
    if (interactable != nullptr && !canInteract(*interactable, currentTimeSeconds)) return kNullEntity;
    return target;
}

void toggleDoor(Door& door, Transform& transform) {
    door.isOpen = !door.isOpen;
    transform.rotation = door.isOpen ? door.openRotation : door.closedRotation;
}

void collectPickup(EntityId entity, ECS& ecs) {
    auto* pickup = ecs.tryGetComponent<Pickup>(entity);
    if (pickup == nullptr || pickup->collected) return;

    pickup->collected = true;
    if (auto* renderable = ecs.tryGetComponent<Renderable>(entity)) renderable->visible = false;
    ecs.removeComponent<Interactable>(entity);
}

} // namespace engine::core
