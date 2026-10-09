#include "studio/plugins/PhysicsPreviewPlugin.hpp"

#include <algorithm>
#include <unordered_set>

#include <imgui.h>

#include "core/Audio.hpp"
#include "core/Components.hpp"
#include "core/InstanceSignals.hpp"
#include "core/Logger.hpp"
#include "core/RobloxPlayers.hpp"
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
    captureScene(ecs);

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

        if (!core::instances::isInWorld(ecs, entity)) continue;
        const bool sensor = !core::instances::canCollide(ecs, entity);
        if (physics_.attachBodyToEntity(entity, ecs, shape, material, motionType, 0.0f, core::CollisionLayer::Default,
                                        sensor)) {
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
        scripting_.setDebugger(scriptDebugger_);
        scriptPhysicsPreviewApi_ = std::make_unique<ScriptPhysicsPreviewApi>(*this, ecs);
        scriptWorldApi_ = std::make_unique<core::ScriptWorldApi>(ecs, physics_, animationPlayer_);
        scriptWorldApi_->setSpawnBoxMeshHandle(spawnBoxMesh_);
        if (audio_ != nullptr) scriptAudioApi_ = std::make_unique<core::ScriptAudioApi>(*audio_, ecs);
        scripting_.setBindingsHook([this](lua_State* L) {
            scriptPhysicsPreviewApi_->registerInto(L);
            scriptWorldApi_->registerInto(L);
            if (scriptAudioApi_) scriptAudioApi_->registerInto(L);
        });
        // Kronos ("Script Editor QoL" -- Engine Console click-to-jump):
        // without this, loadAndRun()'s compile/runtime error messages
        // (which embed the entity's own Name as chunkName -- see
        // core::tickScriptHotReload()) only ever reached stderr, so a
        // real entity script error never showed up in the Engine Log tab
        // at all. Same compile/runtime-vs-everything-else classification
        // DebugConsolePanel::appendLine() already uses for its own REPL
        // output, so both sources land in the Engine Log tab consistently.
        scripting_.setOutputCallback([](const std::string& line) {
            if (line.rfind("compile error", 0) == 0 || line.rfind("runtime error", 0) == 0) {
                core::logError("Script", "%s", line.c_str());
            } else {
                core::logInfo("Script", "%s", line.c_str());
            }
        });
    }

    if (audio_ != nullptr) {
        mixerBeforePlay_ = audio_->mixer().config();
        for (auto [entity, sound] : ecs.raw().view<core::AudioSource>().each()) sound.playing = sound.playOnStart;
    }

    core::RunServiceState& runService = core::signals::runService(ecs);
    runService = core::RunServiceState{};
    runService.studio = true;
    physics_.setTouchRecording(true);

    core::players::reset(ecs);
    playing_ = true;
    paused_ = false;
    if (onPlay_) onPlay_(ecs, physics_);
    statusMessage_ = "Playing -- " + std::to_string(attachedEntities_.size()) + " bodies simulating";
    if (skippedMesh > 0) {
        statusMessage_ += " (" + std::to_string(skippedMesh) + " Mesh collider(s) skipped, see README Known Issues)";
    }
}

void PhysicsPreviewPlugin::stop(core::ECS& ecs) {
    if (!playing_) return;
    if (onStop_) onStop_(ecs);
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
    scriptAudioApi_.reset();
    physics_.setTouchRecording(false);
    core::signals::runService(ecs).running = false;
    if (audio_ != nullptr) {
        for (auto [entity, sound] : ecs.raw().view<core::AudioSource>().each()) {
            sound.playing = false;
            audio_->stopSound(sound.soundHandle);
        }
        audio_->mixer().clearSnapshots();
        (void)audio_->mixer().setConfig(mixerBeforePlay_);
    }
    restoreScene(ecs);

    playing_ = false;
    paused_ = false;
    statusMessage_ = "Stopped -- every entity reverted to its plain, physics-free authored state.";
}

bool PhysicsPreviewPlugin::stepOnce(core::ECS& ecs, float dt) {
    if (!playing_ || !paused_) return false; // see this method's own .hpp comment
    physics_.step(dt, ecs);
    recentContacts_ = physics_.drainCollisionEvents();
    for (const auto& touch : physics_.drainTouchEvents()) core::signals::touch(ecs, touch.a, touch.b, touch.began);
    core::signals::flush(ecs);
    return true;
}

void PhysicsPreviewPlugin::update(float dt, core::ECS& ecs, core::EntityId /*selected*/,
                                   const std::vector<core::EntityId>& /*selectedEntities*/) {
    if (!playing_) return;
    if (scripting_.debugPaused()) {
        scripting_.tick(0.0f);
        return;
    }
    // Same order as the Player's GameLoop (docs/ROBLOX_BRIDGE.md, "Frame order").
    core::signals::renderStepped(ecs, dt);
    core::signals::flush(ecs);
    if (!paused_) core::signals::stepped(ecs, dt);

    // Real hot-reload: a script saved from the Script Editor while
    // Playing gets diffed and (re)loaded here, then ticked -- see
    // core::tickScriptHotReload()'s own comment. Physics/ECS/camera are
    // completely untouched by this.
    core::tickScriptHotReload(ecs, scripting_);
    scripting_.tick(dt);
    core::players::tick(ecs, dt);
    core::signals::flush(ecs);

    if (!paused_) {
        physics_.step(dt, ecs);
        recentContacts_ = physics_.drainCollisionEvents();
        for (const auto& contact : recentContacts_) {
            scripting_.fireCollision(static_cast<uint32_t>(contact.first), static_cast<uint32_t>(contact.second));
        }
        for (const auto& touch : physics_.drainTouchEvents()) core::signals::touch(ecs, touch.a, touch.b, touch.began);
        core::signals::heartbeat(ecs, dt);
        core::signals::flush(ecs);
    }
}

namespace {

void collectMembers(core::ECS& ecs, core::EntityId entity, std::vector<core::EntityId>& out) {
    out.push_back(entity);
    if (const auto* hierarchy = ecs.tryGetComponent<core::Hierarchy>(entity)) {
        for (core::EntityId child : hierarchy->children) {
            if (ecs.raw().valid(child)) collectMembers(ecs, child, out);
        }
    }
}

} // namespace

void PhysicsPreviewPlugin::captureScene(core::ECS& ecs) {
    playSnapshot_ = PlaySnapshot{};
    for (auto [entity, transform] : ecs.raw().view<core::Transform>().each()) {
        playSnapshot_.entities.push_back(entity);
        playSnapshot_.transforms.emplace_back(entity, transform);
        if (const auto* renderable = ecs.tryGetComponent<core::Renderable>(entity)) {
            playSnapshot_.renderables.emplace_back(entity, *renderable);
        }
        if (const auto* light = ecs.tryGetComponent<core::Light>(entity)) playSnapshot_.lights.emplace_back(entity, *light);
        const auto* hierarchy = ecs.tryGetComponent<core::Hierarchy>(entity);
        if (hierarchy == nullptr || hierarchy->parent == core::kNullEntity || !ecs.raw().valid(hierarchy->parent)) {
            PlaySnapshot::Root root;
            collectMembers(ecs, entity, root.members);
            root.snapshot = EntitySnapshot::capture(ecs, {entity});
            playSnapshot_.roots.push_back(std::move(root));
        }
    }
}

void PhysicsPreviewPlugin::restoreScene(core::ECS& ecs) {
    auto& registry = ecs.raw();
    const std::unordered_set<core::EntityId> before(playSnapshot_.entities.begin(), playSnapshot_.entities.end());
    std::vector<core::EntityId> spawned;
    for (auto entity : registry.view<core::Transform>()) {
        if (before.count(entity) == 0) spawned.push_back(entity);
    }
    for (core::EntityId entity : spawned) {
        physics_.detachBody(entity, ecs);
        ecs.destroyEntity(entity);
    }

    for (const PlaySnapshot::Root& root : playSnapshot_.roots) {
        const bool intact = std::all_of(root.members.begin(), root.members.end(),
                                        [&](core::EntityId member) { return registry.valid(member); });
        if (intact) continue;
        for (core::EntityId member : root.members) ecs.destroyEntity(member);
        root.snapshot.restore(ecs);
    }

    for (const auto& [entity, transform] : playSnapshot_.transforms) {
        if (auto* current = ecs.tryGetComponent<core::Transform>(entity)) *current = transform;
    }
    for (const auto& [entity, renderable] : playSnapshot_.renderables) {
        if (auto* current = ecs.tryGetComponent<core::Renderable>(entity)) *current = renderable;
    }
    for (const auto& [entity, light] : playSnapshot_.lights) {
        if (auto* current = ecs.tryGetComponent<core::Light>(entity)) *current = light;
    }
    playSnapshot_ = PlaySnapshot{};
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
        "Play creates a Jolt body for every entity with a ColliderShape and PhysicsMaterial (add them in the Inspector\'s Physics section) and simulates live. Stop restores the authored state.");

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
