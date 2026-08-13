#include "studio/SceneManager.hpp"

#include <cstdio>
#include <filesystem>
#include <unordered_map>

#include "core/Hierarchy.hpp"
#include "core/ObjLoader.hpp"

namespace engine::studio {

namespace {

core::Mesh buildMeshFromSource(const core::MeshSource& source, VmaAllocator allocator, VkDevice device,
                                VkCommandPool cmdPool, VkQueue queue) {
    switch (source.kind) {
        case core::MeshSourceKind::Box:
            return core::Mesh::createBox(allocator, device, cmdPool, queue, source.params);
        case core::MeshSourceKind::Plane:
            return core::Mesh::createPlane(allocator, device, cmdPool, queue, source.params.x, source.params.z);
        case core::MeshSourceKind::Capsule:
            return core::Mesh::createCapsule(allocator, device, cmdPool, queue, source.params.x, source.params.y);
        case core::MeshSourceKind::Quad:
            return core::Mesh::createQuad(allocator, device, cmdPool, queue, source.params.x);
        case core::MeshSourceKind::Obj: {
            core::ObjLoadResult obj = core::loadObj(source.path);
            if (!obj.succeeded) {
                std::fprintf(stderr, "SceneManager: failed to re-import \"%s\": %s\n", source.path.c_str(),
                             obj.error.c_str());
                return core::Mesh{};
            }
            core::Mesh mesh;
            if (!mesh.uploadFromHost(allocator, device, cmdPool, queue, obj.vertices, obj.indices)) {
                std::fprintf(stderr, "SceneManager: GPU upload failed re-importing \"%s\"\n", source.path.c_str());
                return core::Mesh{};
            }
            return mesh;
        }
    }
    return core::Mesh{};
}

} // namespace

core::SceneFile SceneManager::captureScene(core::ECS& ecs, const core::Camera& camera) const {
    core::SceneFile file;
    file.cameraPosition = camera.position;
    file.cameraYawDegrees = camera.yawDegrees;
    file.cameraPitchDegrees = camera.pitchDegrees;
    file.cameraFovDegrees = camera.verticalFovDegrees;

    for (auto entity : ecs.view<core::Transform>()) {
        const core::Name* name = ecs.tryGetComponent<core::Name>(entity);
        if (name == nullptr || name->value.empty()) continue; // unnamed entities can't round-trip, see SceneEntityRecord's comment

        core::SceneEntityRecord record;
        record.name = name->value;

        if (const auto* transform = ecs.tryGetComponent<core::Transform>(entity)) {
            record.position = transform->position;
            record.rotation = transform->rotation;
            record.scale = transform->scale;
        }

        if (const auto* renderable = ecs.tryGetComponent<core::Renderable>(entity)) {
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

        if (const auto* meshSource = ecs.tryGetComponent<core::MeshSource>(entity)) {
            record.hasMeshSource = true;
            record.meshSource = *meshSource;
        }

        if (const auto* emitter = ecs.tryGetComponent<core::ParticleEmitter>(entity)) {
            record.hasParticleEmitter = true;
            record.emitter = emitter->settings;
        }

        if (const auto* light = ecs.tryGetComponent<core::Light>(entity)) {
            record.hasLight = true;
            record.light = *light;
        }

        // Real parent, by name -- see SceneEntityRecord::parentName's own
        // comment on why name (not EntityId) is the only thing a future
        // load can resolve this against. A parent with no name (or an
        // empty one) can't be re-identified on load either, so honestly
        // fall back to saving this entity as a root rather than a
        // reference to a parent the load can never find.
        if (const auto* hierarchy = ecs.tryGetComponent<core::Hierarchy>(entity);
            hierarchy != nullptr && hierarchy->parent != core::kNullEntity) {
            const core::Name* parentName = ecs.tryGetComponent<core::Name>(hierarchy->parent);
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

bool SceneManager::saveScene(const std::string& path, core::ECS& ecs, const core::Camera& camera) {
    core::SceneFile file = captureScene(ecs, camera);
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

bool SceneManager::loadScene(const std::string& path, core::ECS& ecs, core::MeshLibrary& meshLibrary,
                              VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue,
                              core::Camera& camera) {
    core::SceneFile file;
    if (!file.loadFromFile(path)) return false;

    ecs.raw().clear();

    // Real name -> live EntityId map, built alongside creation -- the
    // second pass below (parent resolution) needs every entity to exist
    // first, since a PARENT line can reference an entity that appears
    // later in the file (see SceneEntityRecord::parentName's own comment).
    std::unordered_map<std::string, core::EntityId> entityByName;
    entityByName.reserve(file.entities.size());

    for (const auto& record : file.entities) {
        core::EntityId entity = ecs.createEntity(record.name);
        entityByName[record.name] = entity;
        if (auto* transform = ecs.tryGetComponent<core::Transform>(entity)) {
            transform->position = record.position;
            transform->rotation = record.rotation;
            transform->scale = record.scale;
        }

        if (record.hasRenderable) {
            auto& renderable = ecs.addComponent<core::Renderable>(entity);
            renderable.baseColor = record.baseColor;
            renderable.metallic = record.metallic;
            renderable.roughness = record.roughness;
            renderable.normalIntensity = record.normalIntensity;
            renderable.emissiveColor = record.emissiveColor;
            renderable.emissiveIntensity = record.emissiveIntensity;
            renderable.castsShadow = record.castsShadow;
            renderable.instanced = record.instanced;

            if (record.hasMeshSource) {
                core::Mesh mesh = buildMeshFromSource(record.meshSource, allocator, device, cmdPool, queue);
                if (mesh.vertexBuffer() != VK_NULL_HANDLE) {
                    renderable.meshHandle = meshLibrary.registerMesh(std::move(mesh));
                } else {
                    std::fprintf(stderr, "SceneManager: \"%s\" kept its Renderable but has no mesh (regeneration failed)\n",
                                 record.name.c_str());
                }
                auto& meshSource = ecs.addComponent<core::MeshSource>(entity);
                meshSource = record.meshSource;
            } else {
                std::fprintf(stderr,
                              "SceneManager: \"%s\" has a Renderable but no saved MeshSource -- created with no mesh\n",
                              record.name.c_str());
            }
        }

        if (record.hasParticleEmitter) {
            auto& emitter = ecs.addComponent<core::ParticleEmitter>(entity);
            emitter.settings = record.emitter;
        }

        if (record.hasLight) {
            auto& light = ecs.addComponent<core::Light>(entity);
            light = record.light;
        }
    }

    // Real parent resolution -- second pass, now that every entity in the
    // file exists. Uses core::hierarchy::setParent() itself (not a raw
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
        core::hierarchy::setParent(ecs, childIt->second, parentIt->second);
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

void SceneManager::newScene(core::ECS& ecs) {
    ecs.raw().clear();
    currentScenePath_.clear();
    dirty_ = false;
    autosaveTimer_ = 0.0f;
    lastSeenEntityCount_ = ecs.entityCount();
}

void SceneManager::tickAutosave(float dt, core::ECS& ecs, const core::Camera& camera) {
    size_t currentEntityCount = ecs.entityCount();
    if (currentEntityCount != lastSeenEntityCount_) {
        dirty_ = true;
        lastSeenEntityCount_ = currentEntityCount;
    }

    if (currentScenePath_.empty() || !dirty_) {
        autosaveTimer_ = 0.0f;
        return;
    }

    autosaveTimer_ += dt;
    if (autosaveTimer_ < kAutosaveIntervalSeconds) return;
    autosaveTimer_ = 0.0f;

    core::SceneFile file = captureScene(ecs, camera);
    if (!file.saveToFile(recoveryPathFor(currentScenePath_))) {
        std::fprintf(stderr, "SceneManager: autosave to \"%s\" failed\n", recoveryPathFor(currentScenePath_).c_str());
    }
    // dirty_ deliberately NOT cleared here -- an autosave protects
    // against loss, it isn't equivalent to the user's own Save (which
    // does clear dirty_, see saveScene()).
}

std::string SceneManager::recoveryPathFor(const std::string& scenePath) { return scenePath + ".autosave"; }

bool SceneManager::hasRecoveryFile(const std::string& scenePath) {
    return std::filesystem::exists(recoveryPathFor(scenePath));
}

} // namespace engine::studio
