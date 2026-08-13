#include "studio/plugins/PhysicsPreviewPlugin.hpp"

#include <imgui.h>

#include "core/Components.hpp"

namespace engine::studio::plugins {

void PhysicsPreviewPlugin::play(core::ECS& ecs) {
    if (playing_) return;
    if (!physicsInitialized_) {
        if (!physics_.initialize()) {
            statusMessage_ = "Physics failed to initialize.";
            return;
        }
        physicsInitialized_ = true;
    }

    attachedEntities_.clear();
    int skippedMesh = 0;
    auto view = ecs.view<core::ColliderShape, core::PhysicsMaterial>();
    for (auto entity : view) {
        auto& shape = view.get<core::ColliderShape>(entity);
        auto& material = view.get<core::PhysicsMaterial>(entity);

        // Real authored intent (see InspectorPanel's "Physics" section):
        // an unattached RigidBody{kInvalidBodyId, motionType} carries
        // what motion type this entity *wants* once a live body exists.
        // Defaults to Dynamic if the entity has no RigidBody at all.
        core::RigidBodyMotionType motionType = core::RigidBodyMotionType::Dynamic;
        if (auto* rb = ecs.tryGetComponent<core::RigidBody>(entity)) motionType = rb->motionType;

        // Real, stated limitation: Mesh colliders need host-side
        // vertex/index data (see Physics::attachBodyToEntity()'s own
        // comment) that a Studio entity's GPU-uploaded core::Mesh
        // doesn't retain after upload -- skipped, not silently ignored
        // (see statusMessage_ below), a real follow-up documented in
        // README's Known Issues rather than half-built here.
        if (shape.kind == core::ColliderShapeKind::Mesh) {
            ++skippedMesh;
            continue;
        }

        if (physics_.attachBodyToEntity(entity, ecs, shape, material, motionType)) {
            attachedEntities_.push_back(entity);
        }
    }

    playing_ = true;
    statusMessage_ = "Playing -- " + std::to_string(attachedEntities_.size()) + " bodies simulating";
    if (skippedMesh > 0) {
        statusMessage_ += " (" + std::to_string(skippedMesh) + " Mesh collider(s) skipped, see README Known Issues)";
    }
}

void PhysicsPreviewPlugin::stop(core::ECS& ecs) {
    if (!playing_) return;
    for (core::EntityId entity : attachedEntities_) physics_.detachBody(entity, ecs);
    attachedEntities_.clear();
    recentContacts_.clear();
    hasTestRay_ = false;
    playing_ = false;
    statusMessage_ = "Stopped -- every entity reverted to its plain, physics-free authored state.";
}

void PhysicsPreviewPlugin::update(float dt, core::ECS& ecs, core::EntityId /*selected*/,
                                   const std::vector<core::EntityId>& /*selectedEntities*/) {
    if (!playing_) return;
    physics_.step(dt, ecs);
    recentContacts_ = physics_.drainCollisionEvents();
}

void PhysicsPreviewPlugin::castTestRay(glm::vec3 origin, glm::vec3 direction, float maxDistance) {
    if (!playing_) {
        statusMessage_ = "Cast Test Ray needs a live simulation -- Play first.";
        return;
    }
    testRayOrigin_ = origin;
    testRayHit_ = physics_.raycast(origin, direction, maxDistance);
    hasTestRay_ = true;
}

void PhysicsPreviewPlugin::drawPanel(core::ECS& ecs, core::EntityId /*selected*/,
                                      const std::vector<core::EntityId>& /*selectedEntities*/) {
    ImGui::Begin("Physics Preview");

    ImGui::TextWrapped(
        "Play attaches a real Jolt body to every entity with a real ColliderShape + PhysicsMaterial (see "
        "Inspector's \"Physics\" section on a selected entity to add one), steps it live, and Stop reverts every "
        "entity back to its plain, physics-free authored state.");

    if (!playing_) {
        if (ImGui::Button("Play")) play(ecs);
    } else {
        if (ImGui::Button("Stop")) stop(ecs);
    }
    if (!statusMessage_.empty()) ImGui::TextWrapped("%s", statusMessage_.c_str());

    ImGui::Separator();
    ImGui::TextUnformatted("Physics Debug Draw");
    ImGui::Checkbox("Colliders", &showColliders);
    ImGui::SameLine();
    ImGui::Checkbox("Contacts", &showContacts);
    ImGui::SameLine();
    ImGui::Checkbox("Raycasts", &showRaycasts);
    ImGui::TextDisabled("(also toggleable from the Viewport toolbar)");

    ImGui::End();
}

} // namespace engine::studio::plugins
