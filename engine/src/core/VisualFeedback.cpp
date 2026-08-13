#include "core/VisualFeedback.hpp"

#include <vector>

#include "core/Components.hpp"

namespace engine::core {

void triggerFlash(ECS& ecs, EntityId entity, float peakIntensity, float restingIntensity, float totalSeconds) {
    auto* renderable = ecs.tryGetComponent<Renderable>(entity);
    if (renderable == nullptr) return;

    ecs.addComponent<FlashEffect>(entity, FlashEffect{totalSeconds, totalSeconds, peakIntensity, restingIntensity});
    renderable->emissiveIntensity = peakIntensity;
}

void tickFlashEffects(float dt, ECS& ecs) {
    // Collected first, then removed after the loop -- removing a
    // component that's part of the same multi-component view being
    // iterated can invalidate that view's iterator (the same
    // destroyEntity()-mid-iteration hazard AvatarLoadoutSync.cpp already
    // documents, just for removeComponent<T>() instead).
    std::vector<EntityId> finished;

    auto view = ecs.view<FlashEffect, Renderable>();
    for (auto entity : view) {
        auto& flash = view.get<FlashEffect>(entity);
        auto& renderable = view.get<Renderable>(entity);

        flash.remainingSeconds -= dt;
        if (flash.remainingSeconds <= 0.0f) {
            renderable.emissiveIntensity = flash.restingIntensity;
            finished.push_back(entity);
        } else {
            float t = 1.0f - (flash.remainingSeconds / flash.totalSeconds); // 0 at trigger, 1 at completion
            renderable.emissiveIntensity = flash.peakIntensity + (flash.restingIntensity - flash.peakIntensity) * t;
        }
    }

    for (EntityId entity : finished) ecs.removeComponent<FlashEffect>(entity);
}

} // namespace engine::core
