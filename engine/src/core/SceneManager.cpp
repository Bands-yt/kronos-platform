#include "core/SceneManager.hpp"

#include <cstdio>
#include <filesystem>
#include <unordered_map>

#include "core/Hierarchy.hpp"
#include "core/ObjLoader.hpp"

namespace engine::core {

namespace {

Mesh buildMeshFromSource(const MeshSource& source, VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool,
                          VkQueue queue) {
    switch (source.kind) {
        case MeshSourceKind::Box:
            return Mesh::createBox(allocator, device, cmdPool, queue, source.params);
        case MeshSourceKind::Plane:
            return Mesh::createPlane(allocator, device, cmdPool, queue, source.params.x, source.params.z);
        case MeshSourceKind::Capsule:
            return Mesh::createCapsule(allocator, device, cmdPool, queue, source.params.x, source.params.y);
        case MeshSourceKind::Quad:
            return Mesh::createQuad(allocator, device, cmdPool, queue, source.params.x);
        case MeshSourceKind::Obj: {
            ObjLoadResult obj = loadObj(source.path);
            if (!obj.succeeded) {
                std::fprintf(stderr, "SceneManager: failed to re-import \"%s\": %s\n", source.path.c_str(),
                             obj.error.c_str());
                return Mesh{};
            }
            Mesh mesh;
            if (!mesh.uploadFromHost(allocator, device, cmdPool, queue, obj.vertices, obj.indices)) {
                std::fprintf(stderr, "SceneManager: GPU upload failed re-importing \"%s\"\n", source.path.c_str());
                return Mesh{};
            }
            return mesh;
        }
    }
    return Mesh{};
}

} // namespace

SceneFile SceneManager::captureScene(ECS& ecs, const Camera& camera) const {
    SceneFile file;
    file.cameraPosition = camera.position;
    file.cameraYawDegrees = camera.yawDegrees;
    file.cameraPitchDegrees = camera.pitchDegrees;
    file.cameraFovDegrees = camera.verticalFovDegrees;

    for (auto entity : ecs.view<Transform>()) {
        const Name* name = ecs.tryGetComponent<Name>(entity);
        if (name == nullptr || name->value.empty()) continue; // unnamed entities can't round-trip, see SceneEntityRecord's comment

        SceneEntityRecord record;
        record.name = name->value;

        if (const auto* transform = ecs.tryGetComponent<Transform>(entity)) {
            record.position = transform->position;
            record.rotation = transform->rotation;
            record.scale = transform->scale;
        }

        if (const auto* renderable = ecs.tryGetComponent<Renderable>(entity)) {
            record.hasRenderable = true;
            record.baseColor = renderable->baseColor;
            record.metallic = renderable->metallic;
            record.roughness = renderable->roughness;
            record.normalIntensity = renderable->normalIntensity;
            record.emissiveColor = renderable->emissiveColor;
            record.emissiveIntensity = renderable->emissiveIntensity;
            record.castsShadow = renderable->castsShadow;
            record.instanced = renderable->instanced;
        }

        if (const auto* meshSource = ecs.tryGetComponent<MeshSource>(entity)) {
            record.hasMeshSource = true;
            record.meshSource = *meshSource;
        }

        if (const auto* emitter = ecs.tryGetComponent<ParticleEmitter>(entity)) {
            record.hasParticleEmitter = true;
            record.emitter = emitter->settings;
        }

        if (const auto* light = ecs.tryGetComponent<Light>(entity)) {
            record.hasLight = true;
            record.light = *light;
        }

        // Kronos ("Game Catalogue Overhaul", Phase 2) -- real capture of
        // whatever RigidBody/ColliderShape a caller (e.g. a hand-authored
        // scene, or a live Play-mode session) already put on this entity.
        // PhysicsMaterial is deliberately NOT captured/round-tripped yet
        // -- a real, stated scope gap (same spirit as SceneFile's own
        // "material textures" gap) -- loadScene() below re-attaches
        // physics bodies with PhysicsMaterial{}'s plain defaults.
        if (const auto* rigidBody = ecs.tryGetComponent<RigidBody>(entity)) {
            record.hasRigidBody = true;
            record.motionType = rigidBody->motionType;
        }
        if (const auto* colliderShape = ecs.tryGetComponent<ColliderShape>(entity)) {
            record.hasColliderShape = true;
            record.colliderShape = *colliderShape;
        }

        // Kronos ("Developer Velocity Sprint" -- packaging needs real
        // scripts to bundle, which surfaced this real, pre-existing gap):
        // scriptId/loadedSource are deliberately NOT captured -- see
        // SceneEntityRecord::hasScript's own comment on why they're live-
        // VM bookkeeping, not authored data.
        if (const auto* script = ecs.tryGetComponent<Script>(entity)) {
            record.hasScript = true;
            record.scriptSource = script->source;
            record.scriptAutoRun = script->autoRun;
        }

        // Real parent, by name -- see SceneEntityRecord::parentName's own
        // comment on why name (not EntityId) is the only thing a future
        // load can resolve this against. A parent with no name (or an
        // empty one) can't be re-identified on load either, so honestly
        // fall back to saving this entity as a root rather than a
        // reference to a parent the load can never find.
        if (const auto* hierarchy = ecs.tryGetComponent<Hierarchy>(entity);
            hierarchy != nullptr && hierarchy->parent != kNullEntity) {
            const Name* parentName = ecs.tryGetComponent<Name>(hierarchy->parent);
            if (parentName != nullptr && !parentName->value.empty()) {
                record.parentName = parentName->value;
            } else {
                std::fprintf(stderr,
                              "SceneManager: \"%s\"'s real parent has no name -- saved as a root instead\n",
                              record.name.c_str());
            }
        }

        file.entities.push_back(std::move(record));
    }

    return file;
}

bool SceneManager::saveScene(const std::string& path, ECS& ecs, const Camera& camera) {
    SceneFile file = captureScene(ecs, camera);
    if (!file.saveToFile(path)) return false;

    // A deliberate save supersedes any pending autosave recovery for this
    // path -- otherwise hasRecoveryFile() would keep offering to "recover"
    // content the user just explicitly saved over. remove() only throws
    // on a real filesystem error, never for "the file doesn't exist" (the
    // common case, no recovery was pending) -- (void) discards the bool
    // success flag, there's nothing actionable to do with it here.
    (void)std::filesystem::remove(recoveryPathFor(path));

    currentScenePath_ = path;
    dirty_ = false;
    lastSeenEntityCount_ = ecs.entityCount();
    return true;
}

bool SceneManager::loadScene(const std::string& path, ECS& ecs, MeshLibrary& meshLibrary, VmaAllocator allocator,
                              VkDevice device, VkCommandPool cmdPool, VkQueue queue, Camera& camera,
                              Physics* physics) {
    SceneFile file;
    if (!file.loadFromFile(path)) return false;

    ecs.raw().clear();

    // Real name -> live EntityId map, built alongside creation -- the
    // second pass below (parent resolution) needs every entity to exist
    // first, since a PARENT line can reference an entity that appears
    // later in the file (see SceneEntityRecord::parentName's own comment).
    std::unordered_map<std::string, EntityId> entityByName;
    entityByName.reserve(file.entities.size());

    for (const auto& record : file.entities) {
        EntityId entity = ecs.createEntity(record.name);
        entityByName[record.name] = entity;
        if (auto* transform = ecs.tryGetComponent<Transform>(entity)) {
            transform->position = record.position;
            transform->rotation = record.rotation;
            transform->scale = record.scale;
        }

        if (record.hasRenderable) {
            auto& renderable = ecs.addComponent<Renderable>(entity);
            renderable.baseColor = record.baseColor;
            renderable.metallic = record.metallic;
            renderable.roughness = record.roughness;
            renderable.normalIntensity = record.normalIntensity;
            renderable.emissiveColor = record.emissiveColor;
            renderable.emissiveIntensity = record.emissiveIntensity;
            renderable.castsShadow = record.castsShadow;
            renderable.instanced = record.instanced;

            if (record.hasMeshSource) {
                Mesh mesh = buildMeshFromSource(record.meshSource, allocator, device, cmdPool, queue);
                if (mesh.vertexBuffer() != VK_NULL_HANDLE) {
                    renderable.meshHandle = meshLibrary.registerMesh(std::move(mesh));
                } else {
                    std::fprintf(stderr, "SceneManager: \"%s\" kept its Renderable but has no mesh (regeneration failed)\n",
                                 record.name.c_str());
                }
                auto& meshSource = ecs.addComponent<MeshSource>(entity);
                meshSource = record.meshSource;
            } else {
                std::fprintf(stderr,
                              "SceneManager: \"%s\" has a Renderable but no saved MeshSource -- created with no mesh\n",
                              record.name.c_str());
            }
        }

        if (record.hasParticleEmitter) {
            auto& emitter = ecs.addComponent<ParticleEmitter>(entity);
            emitter.settings = record.emitter;
        }

        if (record.hasLight) {
            auto& light = ecs.addComponent<Light>(entity);
            light = record.light;
        }

        // Kronos ("Game Catalogue Overhaul", Phase 2): only attaches a
        // real, live Jolt body when a real Physics world was passed in --
        // every existing Studio call site (physics == nullptr) is
        // unaffected, matching this function's own header comment.
        if (record.hasRigidBody && record.hasColliderShape && physics != nullptr) {
            std::vector<glm::vec3> meshPositions;
            std::vector<uint32_t> meshIndices;
            const std::vector<glm::vec3>* meshPositionsPtr = nullptr;
            const std::vector<uint32_t>* meshIndicesPtr = nullptr;
            if (record.colliderShape.kind == ColliderShapeKind::Mesh) {
                ObjLoadResult obj = loadObj(record.colliderShape.path);
                if (obj.succeeded) {
                    meshPositions.reserve(obj.vertices.size());
                    for (const auto& v : obj.vertices) meshPositions.push_back(v.position);
                    meshIndices = obj.indices;
                    meshPositionsPtr = &meshPositions;
                    meshIndicesPtr = &meshIndices;
                } else {
                    std::fprintf(stderr, "SceneManager: \"%s\"'s real mesh collider \"%s\" failed to load: %s\n",
                                 record.name.c_str(), record.colliderShape.path.c_str(), obj.error.c_str());
                }
            }
            bool attached = physics->attachBodyToEntity(entity, ecs, record.colliderShape, PhysicsMaterial{},
                                                          record.motionType, 0.0f, CollisionLayer::Default, false,
                                                          meshPositionsPtr, meshIndicesPtr);
            if (!attached) {
                std::fprintf(stderr, "SceneManager: \"%s\" kept its saved physics data but real body attachment failed\n",
                             record.name.c_str());
            }
        }

        if (record.hasScript) {
            auto& script = ecs.addComponent<Script>(entity);
            script.source = record.scriptSource;
            script.autoRun = record.scriptAutoRun;
            // scriptId/loadedSource stay at their real defaults
            // (kInvalidScript/empty) -- a fresh load means no VM has
            // loaded this yet; whichever real Scripting session owns
            // this ECS next (Application::tick()'s hot-reload loop, or
            // PhysicsPreviewPlugin's Play session) real-loads it the same
            // way it would any newly-saved script.
        }
    }

    // Real parent resolution -- second pass, now that every entity in the
    // file exists. Uses hierarchy::setParent() itself (not a raw
    // Hierarchy component write) so a corrupted/hand-edited scene file
    // still gets the same cycle/self-parent rejection a live setParent()
    // call would apply.
    for (const auto& record : file.entities) {
        if (record.parentName.empty()) continue;
        auto childIt = entityByName.find(record.name);
        auto parentIt = entityByName.find(record.parentName);
        if (childIt == entityByName.end() || parentIt == entityByName.end()) {
            std::fprintf(stderr,
                          "SceneManager: \"%s\" real-references parent \"%s\" which wasn't found in this scene -- "
                          "loaded as a root instead\n",
                          record.name.c_str(), record.parentName.c_str());
            continue;
        }
        hierarchy::setParent(ecs, childIt->second, parentIt->second);
    }

    camera.position = file.cameraPosition;
    camera.yawDegrees = file.cameraYawDegrees;
    camera.pitchDegrees = file.cameraPitchDegrees;
    camera.verticalFovDegrees = file.cameraFovDegrees;

    currentScenePath_ = path;
    dirty_ = false;
    lastSeenEntityCount_ = ecs.entityCount();
    return true;
}

void SceneManager::newScene(ECS& ecs) {
    ecs.raw().clear();
    currentScenePath_.clear();
    dirty_ = false;
    autosaveTimer_ = 0.0f;
    sinceLastMajorEditSnapshot_ = kMinMajorEditSnapshotIntervalSeconds;
    lastSeenEntityCount_ = ecs.entityCount();
}

void SceneManager::tickAutosave(float dt, ECS& ecs, const Camera& camera) {
    size_t currentEntityCount = ecs.entityCount();
    bool majorEdit = false;
    if (currentEntityCount != lastSeenEntityCount_) {
        dirty_ = true;
        majorEdit = true;
        lastSeenEntityCount_ = currentEntityCount;
    }

    if (currentScenePath_.empty() || !dirty_) {
        autosaveTimer_ = 0.0f;
        return;
    }

    autosaveTimer_ += dt;
    sinceLastMajorEditSnapshot_ += dt;

    bool periodicDue = autosaveTimer_ >= kAutosaveIntervalSeconds;
    bool majorEditDue = majorEdit && sinceLastMajorEditSnapshot_ >= kMinMajorEditSnapshotIntervalSeconds;
    if (!periodicDue && !majorEditDue) return;

    autosaveTimer_ = 0.0f;
    sinceLastMajorEditSnapshot_ = 0.0f;

    SceneFile file = captureScene(ecs, camera);
    if (!file.saveToFile(recoveryPathFor(currentScenePath_))) {
        std::fprintf(stderr, "SceneManager: autosave to \"%s\" failed\n", recoveryPathFor(currentScenePath_).c_str());
    }
    // Real, rotating multi-slot history -- see SceneHistory's own class
    // comment and tickAutosave()'s header comment on why this fires
    // alongside the single-slot autosave above rather than replacing it.
    SceneHistory::recordSnapshot(currentScenePath_, file);
    // dirty_ deliberately NOT cleared here -- an autosave protects
    // against loss, it isn't equivalent to the user's own Save (which
    // does clear dirty_, see saveScene()).
}

std::string SceneManager::recoveryPathFor(const std::string& scenePath) { return scenePath + ".autosave"; }

bool SceneManager::hasRecoveryFile(const std::string& scenePath) { return std::filesystem::exists(recoveryPathFor(scenePath)); }

} // namespace engine::core
