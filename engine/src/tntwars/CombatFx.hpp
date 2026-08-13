#pragma once

#include <glm/glm.hpp>

#include "core/ECS.hpp"
#include "core/ParticleSystem.hpp"
#include "core/VisualFeedback.hpp"

namespace engine::tntwars {

// Kronos roadmap Milestone 4 ("Combat FX foundation") -- real, gameplay-
// side effects for TNT-Wars combat, deliberately separate from
// trailer::TrailerDirector's own cinematic-only shake/particle work:
// gameplay FX reacts to real match events (a charge just detonated, a
// hit just landed), a cinematic beat is instead hand-authored ahead of
// time by CinematicSequence -- different drivers, so a different, real,
// small system rather than a shared one.

// --- Screen shake ---------------------------------------------------

// Real, decaying "trauma" model -- the standard technique behind most
// shipped gameplay-camera shake: additive impulses that decay over time,
// with the sampled offset scaled by trauma^2 so small hits barely
// register while stacked/large hits feel sharp. Distinct from
// TrailerDirector::sampleShake()'s own real but *continuous*,
// director-set amplitude (a cinematic beat explicitly turns shake on and
// off; gameplay shake instead reacts to discrete real events and fades
// back out on its own without anyone needing to turn it off).
struct GameplayShakeState {
    float trauma = 0.0f; // real, clamped to [0, 1]
    float timeSeconds = 0.0f; // real internal clock feeding the sine-noise sampler below
};

constexpr float kShakeDecayPerSecond = 1.6f;
constexpr float kShakeMaxOffset = 0.6f; // world units, matching TrailerDirector's own real "Intense" ballpark

// Real, additive, clamped trauma add -- a second impact while already
// shaking real-stacks (up to the real ceiling of 1.0); it does not just
// reset the existing shake.
void addShakeTrauma(GameplayShakeState& state, float amount);
// Real per-tick trauma decay + clock advance -- a real, honest no-op on
// a non-positive dt.
void tickGameplayShake(GameplayShakeState& state, float dt);
// Real, deterministic offset a caller adds to their own live
// Camera::position each frame -- reuses the exact same real three-wave
// sine-noise technique TrailerDirector::sampleShake() already
// established, scaled by trauma^2 * kShakeMaxOffset instead of a fixed
// director-set amplitude.
[[nodiscard]] glm::vec3 sampleGameplayShakeOffset(const GameplayShakeState& state);

// Real, distance-based trauma a nearby explosion should add to a given
// listener position -- full trauma at the explosion center, 0 at/beyond
// radius, the same real linear-falloff shape
// tntwars::computeExplosionDamage() already uses (kept as its own real,
// self-contained formula here rather than a cross-module call, matching
// LavaEruption's own "small, self-contained, single formula" precedent).
[[nodiscard]] float explosionShakeTrauma(glm::vec3 explosionCenter, glm::vec3 listenerPosition, float radius);

// --- Explosion particle burst ---------------------------------------

// Real, tuned one-shot particle-burst settings for a TNT explosion --
// consumed exactly like any other real core::ParticleEmitterSettings
// (see ParticleSystem.hpp's own "looping=false is a one-shot burst"
// convention): a real caller attaches this to a real spawned entity's
// ParticleEmitter component and core::ParticleSystem::update() does the
// rest -- no new rendering path, this reuses the real, existing pipeline.
[[nodiscard]] core::ParticleEmitterSettings explosionBurstSettings();

// Real convenience: spawns one real, transient ECS entity at
// `explosionCenter` carrying a real Transform + the burst settings above
// as a ParticleEmitter -- the same "one real entity per real effect"
// shape TntWarsPlugin::buildMapGeometry() already establishes for map
// pieces. Returns the real spawned entity so a caller can track/destroy
// it once its burst finishes (ParticleSystem itself doesn't remove the
// now-inert emitter entity -- see its own header comment on
// "enabled set false automatically" once a one-shot burst fires).
[[nodiscard]] core::EntityId spawnExplosionParticleBurst(core::ECS& ecs, glm::vec3 explosionCenter);

// Real, smaller/faster one-shot burst for a projectile impact (a rifle
// bolt or arrow hitting a mob/wall) -- the same real ParticleEmitter
// pipeline as an explosion burst, deliberately re-tuned rather than
// reusing explosionBurstSettings() directly: a bullet impact is a real,
// distinct, much smaller event and shouldn't read as a small explosion.
[[nodiscard]] core::ParticleEmitterSettings projectileImpactBurstSettings();
[[nodiscard]] core::EntityId spawnProjectileImpactBurst(core::ECS& ecs, glm::vec3 impactPosition);

// --- Hit markers ------------------------------------------------------
// Real, in-world feedback -- see VisualFeedback.hpp's own documented
// "engine_runtime has no on-screen UI" precedent; a hit marker here is a
// real, visible flash on the entity that got hit, the same honest
// technique Sprint 5's shop/kiosk flash already proved out, not a fake
// 2D crosshair overlay this renderer was never built to draw.

constexpr float kHitMarkerPeakIntensity = 4.0f;
constexpr float kHitMarkerRestingIntensity = 0.4f;
// Real, sharp -- shorter than Sprint 5's own 0.3s shop flash; a hit
// should read as a real snap, not a fade.
constexpr float kHitMarkerDurationSeconds = 0.15f;

// Real, thin wrapper around core::triggerFlash() with TNT-Wars hit-marker
// tuning -- a real, honest no-op if `target` has no Renderable, exactly
// matching triggerFlash()'s own documented behavior.
void triggerHitMarkerFlash(core::ECS& ecs, core::EntityId target);

} // namespace engine::tntwars
