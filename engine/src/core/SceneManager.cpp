#include "core/SceneManager.hpp"

#include <cstdio>
#include <filesystem>
#include <unordered_map>

#include "core/FbxLoader.hpp"
#include "core/GltfLoader.hpp"
#include "core/Hierarchy.hpp"
#include "core/ObjLoader.hpp"
#include "core/ResourceManager.hpp"
#include "core/SceneHistory.hpp"
#include "core/WorldStreaming.hpp"

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
        case MeshSourceKind::Torus:
            return Mesh::createTorus(allocator, device, cmdPool, queue, source.params.x, source.params.y);
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
        case MeshSourceKind::Gltf: {
            GltfLoadResult gltf = loadGltf(source.path);
            if (!gltf.succeeded) {
                std::fprintf(stderr, "SceneManager: failed to re-import \"%s\": %s\n", source.path.c_str(),
                             gltf.error.c_str());
                return Mesh{};
            }
            Mesh mesh;
            if (!mesh.uploadFromHost(allocator, device, cmdPool, queue, gltf.vertices, gltf.indices)) {
                std::fprintf(stderr, "SceneManager: GPU upload failed re-importing \"%s\"\n", source.path.c_str());
                return Mesh{};
            }
            return mesh;
        }
        case MeshSourceKind::Fbx: {
            FbxLoadResult fbx = loadFbx(source.path);
            if (!fbx.succeeded) {
                std::fprintf(stderr, "SceneManager: failed to re-import \"%s\": %s\n", source.path.c_str(),
                             fbx.error.c_str());
                return Mesh{};
            }
            Mesh mesh;
            if (!mesh.uploadFromHost(allocator, device, cmdPool, queue, fbx.vertices, fbx.indices)) {
                std::fprintf(stderr, "SceneManager: GPU upload failed re-importing \"%s\"\n", source.path.c_str());
                return Mesh{};
            }
            return mesh;
        }
    }
    return Mesh{};
}

EntityId findNamedEntity(ECS& ecs, const std::string& name) {
    for (auto entity : ecs.view<Name>()) {
        if (ecs.view<Name>().get<Name>(entity).value == name) return entity;
    }
    return kNullEntity;
}

} // namespace

std::string meshSourceKey(const MeshSource& source) {
    char key[96];
    std::snprintf(key, sizeof(key), "%d %a %a %a ", static_cast<int>(source.kind), source.params.x, source.params.y,
                  source.params.z);
    return key + source.path;
}

bool meshSourceFileBacked(MeshSourceKind kind) {
    return kind == MeshSourceKind::Obj || kind == MeshSourceKind::Gltf || kind == MeshSourceKind::Fbx;
}

void instantiateSceneEntities(const std::vector<SceneEntityRecord>& records, ECS& ecs, const SceneBuildContext& context,
                              std::vector<EntityId>* created) {
    const VkDevice device = context.device;
    Physics* physics = context.physics;
    // Real name -> live EntityId map, built alongside creation -- the
    // second pass below (parent resolution) needs every entity to exist
    // first, since a PARENT line can reference an entity that appears
    // later in the file (see SceneEntityRecord::parentName's own comment).
    std::unordered_map<std::string, EntityId> entityByName;
    entityByName.reserve(records.size());
    std::unordered_map<std::string, uint32_t> localShared;
    std::unordered_map<std::string, uint32_t>& sharedMeshes =
        context.sharedMeshes != nullptr ? *context.sharedMeshes : localShared;

    for (const auto& record : records) {
        EntityId entity = ecs.createEntity(record.name);
        entityByName[record.name] = entity;
        if (created != nullptr) created->push_back(entity);
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
            renderable.layers = record.layers;
            renderable.emissiveColor = record.emissiveColor;
            renderable.emissiveIntensity = record.emissiveIntensity;
            renderable.castsShadow = record.castsShadow;
            renderable.instanced = record.instanced;
            if (!record.surfaceGraph.empty()) ecs.addComponent<SurfaceGraphMaterial>(entity).graph = record.surfaceGraph;

            if (record.hasMeshSource) {
                // No device means a headless load (dedicated server): keep the
                // MeshSource for colliders and replication, skip the GPU upload.
                if (device != VK_NULL_HANDLE && meshSourceFileBacked(record.meshSource.kind) && context.resources != nullptr &&
                    context.resources->hasLoader(ResourceKind::Mesh)) {
                    ResourceHandle mesh = context.resources->acquire(ResourceKind::Mesh, record.meshSource.path);
                    renderable.meshHandle = mesh.get();
                    ecs.raw().get_or_emplace<ResourceRefs>(entity).handles.push_back(std::move(mesh));
                } else if (device != VK_NULL_HANDLE) {
                    const std::string sourceKey = meshSourceKey(record.meshSource);
                    auto shared = sharedMeshes.find(sourceKey);
                    Mesh mesh = shared == sharedMeshes.end()
                                    ? buildMeshFromSource(record.meshSource, context.allocator, device, context.cmdPool, context.queue)
                                    : Mesh{};
                    if (shared != sharedMeshes.end()) {
                        renderable.meshHandle = shared->second;
                    } else if (mesh.vertexBuffer() != VK_NULL_HANDLE) {
                        renderable.meshHandle = context.registerMesh ? context.registerMesh(std::move(mesh))
                                                                     : context.meshLibrary->registerMesh(std::move(mesh));
                        sharedMeshes.emplace(sourceKey, renderable.meshHandle);
                    } else {
                        std::fprintf(stderr, "SceneManager: \"%s\" kept its Renderable but has no mesh (regeneration failed)\n",
                                     record.name.c_str());
                    }
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
        } else if (record.hasRigidBody) {
            // No live world (Studio edit mode): keep the authored data so a
            // later Play session or Save still sees it.
            if (record.hasColliderShape) {
                ecs.addComponent<ColliderShape>(entity, record.colliderShape);
                ecs.addComponent<PhysicsMaterial>(entity, PhysicsMaterial{});
            }
            ecs.addComponent<RigidBody>(entity, RigidBody{RigidBody::kInvalidBodyId, record.motionType});
        }

        if (record.hasSound) {
            auto& sound = ecs.addComponent<AudioSource>(entity, record.sound);
            sound.soundHandle = AudioSource::kInvalidHandle;
            sound.playing = false;
            if (!sound.path.empty() && context.resources != nullptr && context.resources->hasLoader(ResourceKind::Audio)) {
                ResourceHandle handle = context.resources->acquire(ResourceKind::Audio, sound.path);
                sound.soundHandle = handle.get();
                ecs.raw().get_or_emplace<ResourceRefs>(entity).handles.push_back(std::move(handle));
            }
        }

        if (!record.visualScript.empty()) ecs.addComponent<VisualScript>(entity).graph = record.visualScript;
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
    for (const auto& record : records) {
        if (record.parentName.empty()) continue;
        auto childIt = entityByName.find(record.name);
        auto parentIt = entityByName.find(record.parentName);
        EntityId parent = parentIt != entityByName.end() ? parentIt->second : findNamedEntity(ecs, record.parentName);
        if (childIt == entityByName.end() || parent == kNullEntity) {
            std::fprintf(stderr,
                          "SceneManager: \"%s\" real-references parent \"%s\" which wasn't found in this scene -- "
                          "loaded as a root instead\n",
                          record.name.c_str(), record.parentName.c_str());
            continue;
        }
        hierarchy::setParent(ecs, childIt->second, parent);
    }
}

bool captureSceneEntity(ECS& ecs, EntityId entity, SceneEntityRecord& out) {
    const Name* name = ecs.tryGetComponent<Name>(entity);
    if (name == nullptr || name->value.empty()) return false; // unnamed entities can't round-trip, see SceneEntityRecord's comment
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
        record.layers = renderable->layers;
        record.emissiveColor = renderable->emissiveColor;
        record.emissiveIntensity = renderable->emissiveIntensity;
        record.castsShadow = renderable->castsShadow;
        record.instanced = renderable->instanced;
        if (const auto* graph = ecs.tryGetComponent<SurfaceGraphMaterial>(entity)) record.surfaceGraph = graph->graph;
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
    if (const auto* visual = ecs.tryGetComponent<VisualScript>(entity)) record.visualScript = visual->graph;
    if (const auto* sound = ecs.tryGetComponent<AudioSource>(entity)) {
        record.hasSound = true;
        record.sound = *sound;
        record.sound.soundHandle = AudioSource::kInvalidHandle;
        record.sound.playing = false;
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
    out = std::move(record);
    return true;
}

SceneFile SceneManager::captureScene(ECS& ecs, const Camera& camera, const cinematic::CameraRail* rail,
                                      const cinematic::Sequence* sequence) const {
    SceneFile file;
    file.cameraPosition = camera.position;
    file.cameraYawDegrees = camera.yawDegrees;
    file.cameraPitchDegrees = camera.pitchDegrees;
    file.cameraFovDegrees = camera.verticalFovDegrees;

    if (rail != nullptr) {
        file.hasCameraRail = true;
        file.railPoints = rail->points();
        file.railSettings = rail->settings();
    }

    if (sequence != nullptr) {
        file.hasSequence = true;
        file.sequenceFrameRate = sequence->frameRate();
        file.sequenceLoopStart = sequence->loopStart();
        file.sequenceLoopEnd = sequence->loopEnd();
        file.sequenceTracks = sequence->tracks();
    }

    for (auto entity : ecs.view<Transform>()) {
        if (ecs.raw().all_of<StreamedCell>(entity)) continue;
        SceneEntityRecord record;
        if (captureSceneEntity(ecs, entity, record)) file.entities.push_back(std::move(record));
    }

    return file;
}

SceneFile SceneManager::captureWholeWorld(ECS& ecs, const Camera& camera) const {
    SceneFile file = captureScene(ecs, camera);
    if (world_ != nullptr) world_->appendAllCells(file);
    return file;
}

bool SceneManager::saveScene(const std::string& path, ECS& ecs, const Camera& camera, const cinematic::CameraRail* rail,
                              const cinematic::Sequence* sequence) {
    SceneFile file = captureScene(ecs, camera, rail, sequence);
    if (!file.saveToFile(path)) return false;
    if (world_ != nullptr && world_->isOpen()) {
        std::string error;
        // Save As copies the world next to the new scene first, so the old one is left as it was.
        const bool cellsSaved = (world_->scenePath() == path || world_->moveTo(path, &error)) && world_->saveCells(&error);
        if (!cellsSaved) {
            std::fprintf(stderr, "SceneManager: \"%s\" saved but its world cells did not: %s\n", path.c_str(), error.c_str());
            return false;
        }
    }

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
                              Physics* physics, cinematic::CameraRail* rail, cinematic::Sequence* sequence) {
    SceneFile file;
    if (!file.loadFromFile(path)) return false;

    if (world_ != nullptr) world_->close();
    ecs.raw().clear();

    SceneBuildContext context;
    context.meshLibrary = &meshLibrary;
    context.allocator = allocator;
    context.device = device;
    context.cmdPool = cmdPool;
    context.queue = queue;
    context.physics = physics;
    context.resources = resources_;
    instantiateSceneEntities(file.entities, ecs, context);

    if (world_ != nullptr && worldResources_ != nullptr && std::filesystem::exists(WorldManifest::pathFor(path))) {
        std::string error;
        if (!world_->open(path, ecs, *worldResources_, context, &error)) {
            std::fprintf(stderr, "SceneManager: \"%s\" has a world that failed to open: %s\n", path.c_str(), error.c_str());
        }
    }

    camera.position = file.cameraPosition;
    camera.yawDegrees = file.cameraYawDegrees;
    camera.pitchDegrees = file.cameraPitchDegrees;
    camera.verticalFovDegrees = file.cameraFovDegrees;

    // Real, full replace -- same "load supersedes whatever was live before"
    // contract as the ECS clear() above. A file with nothing saved
    // (hasCameraRail/hasSequence false) leaves `rail`/`sequence` untouched,
    // matching `physics`'s own "absent means don't touch" shape.
    if (file.hasCameraRail && rail != nullptr) {
        rail->clear();
        for (const auto& p : file.railPoints) rail->addPoint(p);
        rail->setSettings(file.railSettings);
        rail->resetDamping();
    }

    if (file.hasSequence && sequence != nullptr) {
        sequence->mutableTracks() = file.sequenceTracks;
        sequence->setFrameRate(file.sequenceFrameRate);
        sequence->setLoopRegion(file.sequenceLoopStart, file.sequenceLoopEnd);
        sequence->pause();
        sequence->setPlayhead(0.0f);
    }

    currentScenePath_ = path;
    dirty_ = false;
    lastSeenEntityCount_ = ecs.entityCount();
    return true;
}

void SceneManager::newScene(ECS& ecs) {
    if (world_ != nullptr) world_->close();
    ecs.raw().clear();
    currentScenePath_.clear();
    dirty_ = false;
    autosaveTimer_ = 0.0f;
    sinceLastMajorEditSnapshot_ = kMinMajorEditSnapshotIntervalSeconds;
    lastSeenEntityCount_ = ecs.entityCount();
}

void SceneManager::detachFromFile(const ECS& ecs) {
    currentScenePath_.clear();
    dirty_ = false;
    autosaveTimer_ = 0.0f;
    activeTabIndex_ = -1;
    lastSeenEntityCount_ = ecs.entityCount();
}

void SceneManager::tickAutosave(float dt, ECS& ecs, const Camera& camera, const cinematic::CameraRail* rail,
                                 const cinematic::Sequence* sequence) {
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

    SceneFile file = captureScene(ecs, camera, rail, sequence);
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
