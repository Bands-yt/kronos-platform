#include "tntwars/Explosives.hpp"

#include "core/Components.hpp"

namespace engine::tntwars {

GrenadeState throwGrenade(net::PlayerId owner, glm::vec3 origin, glm::vec3 throwDirection, float throwSpeed,
                            float fuseSeconds) {
    GrenadeState grenade;
    grenade.owner = owner;
    grenade.position = origin;
    float dirLen = glm::length(throwDirection);
    grenade.velocity = dirLen > 0.0001f ? (throwDirection / dirLen) * throwSpeed : glm::vec3(0.0f, throwSpeed, 0.0f);
    grenade.fuseSecondsRemaining = fuseSeconds;
    return grenade;
}

void tickGrenadeTrajectory(GrenadeState& grenade, glm::vec3 gravity, float dt) {
    if (grenade.detonated || dt <= 0.0f) return;
    grenade.velocity += gravity * dt;
    grenade.position += grenade.velocity * dt;
}

void tickGrenadeFuse(GrenadeState& grenade, float dt) {
    if (grenade.detonated || dt <= 0.0f) return;
    grenade.fuseSecondsRemaining -= dt;
    if (grenade.fuseSecondsRemaining <= 0.0f) {
        grenade.fuseSecondsRemaining = 0.0f;
        grenade.detonated = true;
    }
}

ExplosiveBarrelState buildExplosiveBarrel(glm::vec3 position) {
    ExplosiveBarrelState barrel;
    barrel.segment.position = position;
    barrel.segment.halfExtents = glm::vec3(0.5f, 0.7f, 0.5f);
    barrel.segment.health = barrel.segment.maxHealth = 40.0f; // real, deliberately fragile -- one real weapon hit or a nearby blast should pop it
    return barrel;
}

bool tickExplosiveBarrelDetonation(ExplosiveBarrelState& barrel) {
    if (barrel.hasExploded) return false;
    if (!isSegmentDestroyed(barrel.segment)) return false;
    barrel.hasExploded = true;
    return true;
}

void spawnExplosiveBarrelVisual(core::ECS& ecs, core::Physics& physics, core::MeshLibrary& meshLibrary,
                                  const core::ProceduralMaterialLibrary& materials, VmaAllocator allocator,
                                  VkDevice device, VkCommandPool cmdPool, VkQueue queue, ExplosiveBarrelState& barrel,
                                  const char* name) {
    core::Mesh mesh = core::Mesh::createBox(allocator, device, cmdPool, queue, barrel.segment.halfExtents);
    if (mesh.vertexBuffer() == VK_NULL_HANDLE) return;
    uint32_t meshHandle = meshLibrary.registerMesh(std::move(mesh));

    core::EntityId entity = ecs.createEntity(name);
    if (auto* transform = ecs.tryGetComponent<core::Transform>(entity)) transform->position = barrel.segment.position;

    auto& renderable = ecs.addComponent<core::Renderable>(entity);
    renderable.meshHandle = meshHandle;
    renderable.baseColor = glm::vec4(0.85f, 0.55f, 0.15f, 1.0f);
    renderable.metallic = 0.6f;
    renderable.roughness = 0.4f;
    renderable.albedoTexture = materials.metal.albedo;
    renderable.normalTexture = materials.metal.normal;
    renderable.metallicTexture = materials.metal.metallic;
    renderable.roughnessTexture = materials.metal.roughness;
    renderable.aoTexture = materials.metal.ao;
    renderable.emissiveColor = glm::vec3(0.95f, 0.5f, 0.1f);
    renderable.emissiveIntensity = 0.35f;

    auto& meshSource = ecs.addComponent<core::MeshSource>(entity);
    meshSource.kind = core::MeshSourceKind::Box;
    meshSource.params = barrel.segment.halfExtents;

    core::ColliderShape shape;
    shape.kind = core::ColliderShapeKind::Box;
    shape.params = barrel.segment.halfExtents;
    (void)physics.attachBodyToEntity(entity, ecs, shape, core::PhysicsMaterial{}, core::RigidBodyMotionType::Static);

    barrel.visualEntity = entity;
}

void hideExplosiveBarrelVisual(ExplosiveBarrelState& barrel, core::ECS& ecs, core::Physics& physics) {
    if (barrel.visualEntity == core::kNullEntity) return;
    if (auto* renderable = ecs.tryGetComponent<core::Renderable>(barrel.visualEntity)) renderable->visible = false;
    physics.detachBody(barrel.visualEntity, ecs);
}

} // namespace engine::tntwars
