#pragma once

#include <functional>

#include <glm/glm.hpp>

#include "core/ECS.hpp"

namespace engine::despair {

// PROJECT: DESPAIR -- Agent David Miller's sanity meter. Attached to the
// player entity; SanitySystem::update() is the only real writer of
// `current` (LootSystem's Anti-Psychotic Injector pickup and any other
// one-off shock/relief go through applyInstantDelta() below, never touch
// `current` directly, same "one real writer" convention core::Physics
// keeps for Transform).
struct SanityState {
    float current = 100.0f;
    float max = 100.0f;

    // Real, honest "how long has some hazard been actively gazing at me,
    // uninterrupted" accumulator -- drives SanityHazard::gazeExponential
    // below (The Parallel Stalker's "drains exponentially when stared
    // at" requirement). Resets to 0 the instant no hazard gazes this
    // tick; SanitySystem owns writing this, same single-writer rule as
    // `current`.
    float activeGazeSeconds = 0.0f;

    // Edge-triggered latch for the hallucination threshold (spawns
    // Despair Cullers, per that AI's own spec, only while sanity < 20%).
    // Kept here (not recomputed ad hoc by callers) so
    // onHallucinationThresholdCrossed fires exactly once per crossing,
    // not once per frame spent below the line.
    bool hallucinating = false;

    static constexpr float kHallucinationThreshold = 20.0f;
};

// A source of sanity drain: an anomaly, a lurking entity, a corpse --
// anything that damages David's sanity by proximity or by being looked
// at. Attach this to whatever entity should drain sanity; SanitySystem
// makes no assumption about what kind of entity carries it (The Parallel
// Stalker, a static anomaly prop, and a future hazard type all just
// attach one of these).
struct SanityHazard {
    bool enabled = true;

    // Ambient drain per second while the player is within `radius`,
    // regardless of whether they're looking at it.
    float proximityDrainPerSecond = 3.0f;
    float radius = 8.0f;

    // Extra drain while the player is actively gazing at this hazard
    // (dot product of the player's forward vector and the direction to
    // this hazard's position exceeds gazeCosThreshold -- see
    // SanitySystem::update()'s comment for why forward is read off
    // Transform::rotation).
    float gazeDrainPerSecond = 6.0f;
    float gazeCosThreshold = 0.85f; // ~31 degree half-angle cone

    // The Parallel Stalker's own requirement: gaze drain grows the
    // longer the stare is sustained, not a flat per-second rate. false
    // (the default) keeps gazeDrainPerSecond flat for hazards that don't
    // ask for this (a static anomaly prop has no reason to escalate).
    bool gazeExponential = false;
    float gazeExponentialRatePerSecond = 0.4f;
};

// The Mic-Listener Noise Vector -- how "loud" David currently is, on a
// simple ambient scale ([0, 1], not decibels: this engine has no real
// audio-analysis pipeline, and a normalized ratio is all TormentorAI's
// investigate-noise behavior needs). FPSPlayerController (not built yet)
// is the intended writer -- movement speed and, if the platform's real
// mic input is wired up, live input amplitude both feed into it -- same
// "component is the seam, controller writes it later" split SanityState
// itself uses. Attach to the player entity; SanitySystem does not read
// or write this itself (it belongs to AI perception, not sanity drain),
// it just lives alongside SanityState so both player-perception
// components are declared in one place.
struct PlayerNoiseLevel {
    float current = 0.0f; // [0, 1]: 0 = silent/crouched-still, 1 = sprinting/shouting
};

// Real, ECS-driven sanity system. Deliberately reads only Transform +
// Light + SanityHazard -- NOT FPSPlayerController (doesn't exist yet)
// and NOT any concrete AI class -- so this is fully testable today with
// hand-placed entities, and StalkerAI/DespairCullerAI/etc. plug in later
// by just attaching a SanityHazard to themselves, no change needed here.
class SanitySystem {
public:
    void update(float dt, core::ECS& ecs);

    // The one real writer for anything outside this system that needs to
    // move sanity directly -- the Anti-Psychotic Injector's +50, a
    // scripted shock event, etc. Clamped to [0, max].
    void applyInstantDelta(core::ECS& ecs, core::EntityId entity, float delta);

    // Takes a non-const ECS& (not const&) purely because core::ECS itself
    // exposes no const accessors (see ECS.hpp -- tryGetComponent/view are
    // both non-const there too); this reads, never mutates.
    [[nodiscard]] bool isHallucinating(core::ECS& ecs, core::EntityId entity) const;

    // Real illumination probe at a world position -- sums every enabled
    // Light component's contribution with inverse-square falloff capped
    // at that light's own radius, via
    // core::hierarchy::computeWorldMatrix() so a parented light (e.g. one
    // rigged to a MovingPlatform) is measured at its real current world
    // position, matching the same resolution Light's own header comment
    // documents for the renderer. Exposed (not just used internally) so
    // a future FacilityMapBuilder / HidingSpotComponent can reuse the
    // exact same "is this spot dark" definition SanitySystem itself
    // drains sanity against, instead of inventing a second one.
    [[nodiscard]] float computeIlluminationAt(core::ECS& ecs, const glm::vec3& worldPos) const;

    // Below this illumination level, a spot counts as "dark" for
    // kDarknessDrainPerSecond purposes.
    static constexpr float kDarknessThreshold = 0.15f;
    static constexpr float kDarknessDrainPerSecond = 4.0f;

    using ThresholdCallback = std::function<void(core::EntityId entity, float sanity)>;
    // Fires exactly once per crossing below SanityState::kHallucinationThreshold
    // (not once per frame spent under it) -- HorrorAIManager's future
    // Despair Culler spawn logic hooks this instead of polling every tick.
    void setOnHallucinationThresholdCrossed(ThresholdCallback callback);

private:
    ThresholdCallback onHallucinationThresholdCrossed_;
};

} // namespace engine::despair
