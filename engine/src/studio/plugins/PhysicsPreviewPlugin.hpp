#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "core/Physics.hpp"
#include "core/Scripting.hpp"
#include "studio/IStudioPlugin.hpp"

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
    void play(core::ECS& ecs);
    void stop(core::ECS& ecs);

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
    bool physicsInitialized_ = false;
    bool playing_ = false;
    std::vector<core::EntityId> attachedEntities_;
    std::vector<core::Physics::CollisionEvent> recentContacts_;
    std::string statusMessage_;

    bool hasTestRay_ = false;
    glm::vec3 testRayOrigin_{0.0f};
    core::Physics::RaycastHit testRayHit_;
};

} // namespace engine::studio::plugins
