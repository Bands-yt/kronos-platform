#include "studio/CreatorToolsSpawning.hpp"

#include "core/Components.hpp"
#include "core/Interactable.hpp"
#include "core/Navigation.hpp"

namespace engine::studio {

namespace {
// Real, distinct per-kind base colors -- mirrors main.cpp's own bring-up
// scene prop palette (see that file's PropSpec table) so a prop placed
// here and one placed by a real runtime spawn look the same.
glm::vec4 basePropColor(core::WorldPropKind kind) {
    switch (kind) {
        case core::WorldPropKind::Tree: return {0.35f, 0.25f, 0.15f, 1.0f};
        case core::WorldPropKind::Rock: return {0.45f, 0.44f, 0.42f, 1.0f};
        case core::WorldPropKind::Crate: return {0.6f, 0.45f, 0.25f, 1.0f};
        case core::WorldPropKind::Barrel: return {0.3f, 0.35f, 0.3f, 1.0f};
        case core::WorldPropKind::Lamp: return {0.9f, 0.85f, 0.6f, 1.0f};
        case core::WorldPropKind::Bush: return {0.2f, 0.4f, 0.15f, 1.0f};
    }
    return {0.7f, 0.7f, 0.7f, 1.0f};
}
} // namespace

core::EntityId spawnPropAuthoring(core::ECS& ecs, core::WorldPropKind kind, glm::vec3 position, int spawnIndex,
                                   uint32_t boxMeshHandle, uint32_t capsuleMeshHandle) {
    std::string entityName = std::string(core::worldPropKindName(kind)) + " " + std::to_string(spawnIndex);
    core::EntityId entity = ecs.createEntity(entityName);

    if (auto* transform = ecs.tryGetComponent<core::Transform>(entity)) {
        transform->position = position;
    }

    auto& renderable = ecs.addComponent<core::Renderable>(entity);
    renderable.meshHandle = kind == core::WorldPropKind::Tree ? capsuleMeshHandle : boxMeshHandle;
    renderable.baseColor = basePropColor(kind);
    renderable.roughness = 0.85f;

    auto& meshSource = ecs.addComponent<core::MeshSource>(entity);
    if (kind == core::WorldPropKind::Tree) {
        meshSource.kind = core::MeshSourceKind::Capsule;
        meshSource.params = {0.35f, 1.0f, 0.0f};
    } else {
        meshSource.kind = core::MeshSourceKind::Box;
        meshSource.params = {0.5f, 0.5f, 0.5f};
    }

    ecs.addComponent<core::WorldProp>(entity, core::WorldProp{kind});

    // Same three interactive kinds main.cpp's own bring-up scene opts
    // in (Crate/Barrel/Lamp) -- Tree/Rock/Bush stay inert set dressing.
    if (kind == core::WorldPropKind::Lamp || kind == core::WorldPropKind::Crate || kind == core::WorldPropKind::Barrel) {
        auto& interactable = ecs.addComponent<core::Interactable>(entity);
        interactable.proximityEnabled = true;
        interactable.proximityRadius = 2.0f;
        interactable.prompt = kind == core::WorldPropKind::Lamp ? "Press E to toggle" : "Press E to interact";
        if (kind == core::WorldPropKind::Lamp) {
            ecs.addComponent<core::LampState>(entity, core::LampState{});
        }
    }

    return entity;
}

core::EntityId spawnTeleportPadAuthoring(core::ECS& ecs, glm::vec3 position, glm::vec3 destination,
                                          const std::string& linkTag, int spawnIndex, uint32_t boxMeshHandle) {
    std::string entityName = "TeleportPad " + std::to_string(spawnIndex);
    core::EntityId entity = ecs.createEntity(entityName);

    if (auto* transform = ecs.tryGetComponent<core::Transform>(entity)) {
        transform->position = position;
        transform->scale = {1.2f, 0.15f, 1.2f};
    }

    auto& renderable = ecs.addComponent<core::Renderable>(entity);
    renderable.meshHandle = boxMeshHandle;
    renderable.baseColor = {0.25f, 0.55f, 0.85f, 1.0f};
    renderable.metallic = 0.3f;
    renderable.roughness = 0.3f;
    auto& meshSource = ecs.addComponent<core::MeshSource>(entity);
    meshSource.kind = core::MeshSourceKind::Box;
    meshSource.params = {0.5f, 0.5f, 0.5f};

    auto& interactable = ecs.addComponent<core::Interactable>(entity);
    interactable.prompt = "Press E to teleport";
    interactable.cooldownSeconds = 1.0f;

    core::TeleportPad pad;
    pad.destination = destination;
    pad.linkTag = linkTag;
    ecs.addComponent<core::TeleportPad>(entity, pad);
    ecs.addComponent<core::NavMarker>(entity, core::NavMarker{core::NavMarkerKind::TeleportPad, entityName});

    return entity;
}

core::EntityId spawnNavMarkerAuthoring(core::ECS& ecs, glm::vec3 position, core::NavMarkerKind kind,
                                        const std::string& label, int spawnIndex) {
    std::string entityName = label.empty() ? ("NavMarker " + std::to_string(spawnIndex)) : label;
    core::EntityId entity = ecs.createEntity(entityName);
    if (auto* transform = ecs.tryGetComponent<core::Transform>(entity)) {
        transform->position = position;
    }
    ecs.addComponent<core::NavMarker>(entity, core::NavMarker{kind, label});
    return entity;
}

core::EntityId spawnPointLightAuthoring(core::ECS& ecs, glm::vec3 position, int spawnIndex, uint32_t boxMeshHandle) {
    std::string entityName = "PointLight " + std::to_string(spawnIndex);
    core::EntityId entity = ecs.createEntity(entityName);

    if (auto* transform = ecs.tryGetComponent<core::Transform>(entity)) {
        transform->position = position;
        transform->scale = {0.2f, 0.2f, 0.2f}; // small -- a visible handle, not a real light fixture prop
    }

    auto& renderable = ecs.addComponent<core::Renderable>(entity);
    renderable.meshHandle = boxMeshHandle;
    renderable.baseColor = {1.0f, 1.0f, 1.0f, 1.0f};
    renderable.emissiveColor = {1.0f, 0.95f, 0.85f};
    renderable.emissiveIntensity = 2.0f;
    auto& meshSource = ecs.addComponent<core::MeshSource>(entity);
    meshSource.kind = core::MeshSourceKind::Box;
    meshSource.params = {0.5f, 0.5f, 0.5f};

    ecs.addComponent<core::Light>(entity);
    return entity;
}

} // namespace engine::studio
