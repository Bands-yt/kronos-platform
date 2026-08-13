#include "core/WorldProp.hpp"

#include "core/CollisionLayers.hpp"
#include "core/Components.hpp"
#include "core/Interactable.hpp"
#include "core/Physics.hpp"

namespace engine::core {

const char* worldPropKindName(WorldPropKind kind) {
    switch (kind) {
        case WorldPropKind::Tree: return "Tree";
        case WorldPropKind::Rock: return "Rock";
        case WorldPropKind::Crate: return "Crate";
        case WorldPropKind::Barrel: return "Barrel";
        case WorldPropKind::Lamp: return "Lamp";
        case WorldPropKind::Bush: return "Bush";
    }
    return "Tree";
}

float toggleLamp(LampState& lamp) {
    lamp.on = !lamp.on;
    return lamp.on ? lamp.litIntensity : 0.0f;
}

namespace {
// Static for fixed scenery, Dynamic for physically pushable props -- the
// real, kind-appropriate default every spawnWorldProp() call gets
// without the caller having to know or repeat it per call site.
RigidBodyMotionType motionTypeForKind(WorldPropKind kind) {
    switch (kind) {
        case WorldPropKind::Crate:
        case WorldPropKind::Barrel: return RigidBodyMotionType::Dynamic;
        default: return RigidBodyMotionType::Static;
    }
}
} // namespace

EntityId spawnWorldProp(ECS& ecs, Physics& physics, const WorldPropSpawnInfo& info) {
    EntityId entity = ecs.createEntity("WorldProp");
    if (auto* transform = ecs.tryGetComponent<Transform>(entity)) transform->position = info.position;

    RigidBodyMotionType motionType = motionTypeForKind(info.kind);
    ColliderShape shape;
    shape.kind = ColliderShapeKind::Box;
    shape.params = info.halfExtent;
    // Dynamic props need a real mass; mass<=0 means "compute it for real
    // from material.density and the box's own volume" (see
    // Physics::resolveMass()'s established contract), not a hardcoded
    // placeholder.
    (void)physics.attachBodyToEntity(entity, ecs, shape, info.material, motionType, 0.0f, CollisionLayer::Default);

    auto& renderable = ecs.addComponent<Renderable>(entity);
    renderable.meshHandle = info.meshHandle;
    renderable.baseColor = glm::vec4(info.baseColor, 1.0f);
    renderable.metallic = info.metallic;
    renderable.roughness = info.roughness;

    ecs.addComponent<WorldProp>(entity, WorldProp{info.kind});

    if (info.interactive) {
        auto& interactable = ecs.addComponent<Interactable>(entity);
        interactable.prompt = info.kind == WorldPropKind::Lamp ? "Press E to toggle lamp" : "Press E to interact";
        interactable.cooldownSeconds = 0.3f;

        if (info.kind == WorldPropKind::Lamp) {
            LampState lamp;
            renderable.emissiveColor = info.baseColor;
            renderable.emissiveIntensity = lamp.on ? lamp.litIntensity : 0.0f;
            ecs.addComponent<LampState>(entity, lamp);
        }
    }

    return entity;
}

} // namespace engine::core
