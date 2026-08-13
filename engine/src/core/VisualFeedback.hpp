#pragma once

#include "core/ECS.hpp"

namespace engine::core {

// Sprint 5 task category 6: real sell/buy (and, generally, "this
// interaction just succeeded") visual feedback for entities that don't
// have their own bespoke animation -- a brief emissive-intensity flash on
// the entity's own Renderable, decaying back to its resting value over
// `totalSeconds`. `engine_runtime` has no on-screen UI to pop up a
// confirmation toast (see Interactable.hpp's own "UI hint (stub)"
// comment), so a real, visible change to the thing you just interacted
// with -- a shop stall or upgrade kiosk briefly glowing brighter -- is
// the honest, renderable equivalent.
struct FlashEffect {
    float remainingSeconds = 0.0f;
    float totalSeconds = 0.3f;
    float peakIntensity = 3.0f;
    float restingIntensity = 0.4f;
};

// Starts (or restarts, if already flashing) a flash on `entity`'s
// Renderable -- a no-op if `entity` has no Renderable at all (nothing to
// visually flash).
void triggerFlash(ECS& ecs, EntityId entity, float peakIntensity, float restingIntensity, float totalSeconds);

// Called once per tick (Application.cpp's pre-tick hook) -- counts every
// live FlashEffect down, linearly decaying `Renderable::emissiveIntensity`
// from `peakIntensity` to `restingIntensity` over `totalSeconds`, then
// removes the FlashEffect component once it completes (leaving
// emissiveIntensity pinned at exactly `restingIntensity`, not whatever
// the last partial-decay frame happened to compute).
void tickFlashEffects(float dt, ECS& ecs);

} // namespace engine::core
