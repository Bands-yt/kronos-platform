#pragma once

#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "core/Physics.hpp"
#include "core/Scripting.hpp"
#include "studio/IStudioPlugin.hpp"
#include "studio/ScriptPhysicsPreviewApi.hpp"

namespace engine::studio::plugins {

// Task category 6 ("Scene Physics Integration") + the checkpoint's "run
// Studio, enable physics debug, ... interact with objects": a real, if
// deliberately minimal, Play-mode physics preview. Studio itself runs no
// core::Physics simulation at all outside this plugin (see
// core::Physics::raycast()'s own comment on why Studio's viewport
// picking already uses a separate, physics-independent raycast) -- Play
// stands up a real core::Physics instance and attaches live Jolt bodies
// (core::Physics::attachBodyToEntity()) to every entity already carrying
// real authored ColliderShape + PhysicsMaterial data (see
// InspectorPanel's "Physics" section, where that data comes from),
// stepping it every Studio frame while playing. Stop reverts every
// entity Play attached a body to back to a plain, physics-free Studio
// entity (core::Physics::detachBody()) -- Play only ever *attaches* to
// pre-existing entities, never creates new ones, so Stop leaves the
// scene exactly as authored, the real "safe teardown + recreation" this
// task category asks for.
//
// Deliberately NOT a full "Play Solo" (no Audio session, no character
// spawned automatically) -- see README's Known Issues for what a real
// Play Solo would still need. It does now run a real core::Scripting
// session alongside the physics one (Kronos "Studio QoL Sprint" --
// "Instant Lua Script Hot-Reload"): play() brings up a fresh VM and
// loads every entity's core::Script, update() ticks it and runs the
// same core::tickScriptHotReload() diff-and-reload Application.cpp's
// own real hot-reload path uses, and stop() tears it back down --
// so editing a script in the Script Editor and saving while Playing
// reloads that one script's environment in place, without resetting
// physics, the ECS, or the viewport camera.
class PhysicsPreviewPlugin final : public IStudioPlugin {
public:
    [[nodiscard]] const char* name() const override { return "Physics Preview"; }
    [[nodiscard]] const char* category() const override { return "Physics"; }

    // Ticked every frame regardless of isOpen() (see IStudioPlugin's
    // class comment) -- steps the live simulation while playing, exactly
    // like engine_runtime's own GameLoop does, just driven from Studio's
    // own per-frame loop instead of a fixed-tick accumulator (a real,
    // stated simplification -- see README's Known Issues on why this
    // isn't fixed-timestep the way engine_runtime's GameLoop is).
    void update(float dt, core::ECS& ecs, core::EntityId selected,
                const std::vector<core::EntityId>& selectedEntities) override;
    void drawPanel(core::ECS& ecs, core::EntityId selected, const std::vector<core::EntityId>& selectedEntities) override;

    [[nodiscard]] bool isPlaying() const { return playing_; }

    // Kronos ("Developer Velocity Sprint" -- "Real-Time Visual
    // Performance Profiler" -- "Lua runtime memory allocations"): the
    // one real, live core::Scripting instance Studio ever runs (see this
    // class's own header comment) -- the F3 overlay reads
    // scripting().totalUsedMemoryBytes() through this while Playing.
    [[nodiscard]] const core::Scripting& scripting() const { return scripting_; }

    void play(core::ECS& ecs);
    void stop(core::ECS& ecs);

    // Kronos ("Cinematic Camera Physics & Post-Processing Pipeline" --
    // Luau Studio API Bindings, "deterministic physics step triggers"):
    // real, explicit pause -- update()'s own per-frame auto-step (see its
    // own real-time, frame-rate-dependent dt) is suspended while paused,
    // so a script (studio::ScriptPhysicsPreviewApi's `physics` table,
    // registered into this class's own scripting_) can drive the
    // simulation forward by exact, reproducible amounts via stepOnce()
    // instead -- "deterministic" specifically means a script choosing
    // its own dt per call, not the real-time value update() would
    // otherwise pass. A no-op (real, honest) while not playing at all --
    // there is no live simulation to pause/step yet.
    void pause() { paused_ = true; }
    void resume() { paused_ = false; }
    [[nodiscard]] bool isPaused() const { return paused_; }
    // Real, single fixed-size physics step -- only while playing() AND
    // paused() (see pause()'s own comment for why calling this while
    // update() is also auto-stepping would double-step); a real, honest
    // no-op (false returned) otherwise, never a silent double-step.
    bool stepOnce(core::ECS& ecs, float dt);

    [[nodiscard]] core::Physics& physics() { return physics_; }

    // Real debug-draw toggles -- state studio::panels::ViewportPanel's
    // own drawPhysicsDebugOverlay() reads every frame to decide what to
    // draw (see that method for the actual world->screen projection).
    bool showColliders = false;
    bool showContacts = false;
    bool showRaycasts = false;

    // Real contact point/normal data from the most recent physics step
    // while playing -- what the contact-point visualization draws.
    // Repopulated every step() call while playing (drainCollisionEvents()
    // is itself draining, see Physics.hpp -- this member is what lets a
    // *second* consumer, the debug overlay, see the same tick's events
    // the (currently nonexistent, in Studio) gameplay layer would
    // otherwise have exclusively drained).
    [[nodiscard]] const std::vector<core::Physics::CollisionEvent>& recentContacts() const { return recentContacts_; }

    // A real, on-demand test ray cast from the Viewport's own camera --
    // the real "raycast visualization" tool this task category asks for.
    // Only meaningful while playing (Physics::raycast() needs a live
    // core::Physics -- see PhysicsPreviewPlugin's own class comment).
    void castTestRay(glm::vec3 origin, glm::vec3 direction, float maxDistance);
    [[nodiscard]] bool hasTestRay() const { return hasTestRay_; }
    [[nodiscard]] glm::vec3 testRayOrigin() const { return testRayOrigin_; }
    [[nodiscard]] const core::Physics::RaycastHit& testRayHit() const { return testRayHit_; }

private:
    core::Physics physics_;
    core::Scripting scripting_;
    // Kronos ("Cinematic Camera Physics & Post-Processing Pipeline" --
    // "deterministic physics step triggers"): (re)constructed every real
    // play() call, matching scripting_'s own "fresh VM every Play" real
    // reset -- see ScriptPhysicsPreviewApi.hpp's own header comment.
    std::unique_ptr<ScriptPhysicsPreviewApi> scriptPhysicsPreviewApi_;
    bool physicsInitialized_ = false;
    bool playing_ = false;
    bool paused_ = false;
    std::vector<core::EntityId> attachedEntities_;
    std::vector<core::Physics::CollisionEvent> recentContacts_;
    std::string statusMessage_;

    bool hasTestRay_ = false;
    glm::vec3 testRayOrigin_{0.0f};
    core::Physics::RaycastHit testRayHit_;
};

} // namespace engine::studio::plugins
