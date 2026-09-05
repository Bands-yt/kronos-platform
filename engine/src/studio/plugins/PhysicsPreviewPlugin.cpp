#include "studio/plugins/PhysicsPreviewPlugin.hpp"

#include <imgui.h>

#include "core/Components.hpp"
#include "core/ScriptHotReload.hpp"

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

    // Real, fresh scripting session every Play (mirrors
    // studio::plugins::ScriptedPlugin::reload()'s own shutdown()+
    // initialize() idiom) -- core::tickScriptHotReload() below then
    // real-loads every entity's core::Script the same way a freshly-
    // started engine_runtime session would (source != loadedSource is
    // trivially true the first time, since loadedSource was just reset
    // by the previous stop()).
    if (!scripting_.initialize()) {
        statusMessage_ = "Scripting failed to initialize -- scripts will not run this session.";
    } else {
        // Kronos ("Cinematic Camera Physics & Post-Processing Pipeline"
        // -- "deterministic physics step triggers"): a real, fresh
        // `physics` table for this real, fresh VM -- see
        // ScriptPhysicsPreviewApi.hpp's own header comment for why this,
        // not DebugConsolePanel's separate VM, is the one real place a
        // script can pause/resume/step this simulation.
        scriptPhysicsPreviewApi_ = std::make_unique<ScriptPhysicsPreviewApi>(*this, ecs);
        scripting_.setBindingsHook([this](lua_State* L) { scriptPhysicsPreviewApi_->registerInto(L); });
    }

    playing_ = true;
    paused_ = false;
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

    // Real teardown, matching the physics detach above: every entity's
    // Script goes back to "never loaded" so the *next* Play starts a
    // fresh scriptId in the fresh VM scripting_.shutdown() is about to
    // tear down, instead of core::tickScriptHotReload() wrongly thinking
    // an unchanged `source` means nothing needs (re)loading.
    auto scriptView = ecs.view<core::Script>();
    for (auto entity : scriptView) {
        core::Script& script = scriptView.get<core::Script>(entity);
        script.scriptId = core::kInvalidScript;
        script.loadedSource.clear();
    }
    scripting_.shutdown();

    playing_ = false;
    paused_ = false;
    statusMessage_ = "Stopped -- every entity reverted to its plain, physics-free authored state.";
}

bool PhysicsPreviewPlugin::stepOnce(core::ECS& ecs, float dt) {
    if (!playing_ || !paused_) return false; // see this method's own .hpp comment
    physics_.step(dt, ecs);
    recentContacts_ = physics_.drainCollisionEvents();
    return true;
}

void PhysicsPreviewPlugin::update(float dt, core::ECS& ecs, core::EntityId /*selected*/,
                                   const std::vector<core::EntityId>& /*selectedEntities*/) {
    if (!playing_) return;
    if (!paused_) {
        physics_.step(dt, ecs);
        recentContacts_ = physics_.drainCollisionEvents();
    }

    // Real hot-reload: a script saved from the Script Editor while
    // Playing gets diffed and (re)loaded here, then ticked -- see
    // core::tickScriptHotReload()'s own comment. Physics/ECS/camera are
    // completely untouched by this.
    core::tickScriptHotReload(ecs, scripting_);
    scripting_.tick(dt);
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
        ImGui::SameLine();
        // Kronos ("Cinematic Camera Physics & Post-Processing Pipeline" --
        // "deterministic physics step triggers"): the real UI half of
        // pause()/resume()/stepOnce() -- see those methods' own .hpp
        // comments. A script (studio::ScriptPhysicsPreviewApi's `physics`
        // table) can drive the same real state these buttons do.
        if (paused_) {
            if (ImGui::Button("Resume")) resume();
            ImGui::SameLine();
            if (ImGui::Button("Step One Frame")) stepOnce(ecs, 1.0f / 60.0f);
        } else {
            if (ImGui::Button("Pause")) pause();
        }
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
