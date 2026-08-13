#include "tntwars/ProjectileVisual.hpp"

#include <cmath>

#include "core/Components.hpp"

namespace engine::tntwars {

namespace {

// Real, distinct per-type bolt color -- a Striker's Rocket and a
// Saboteur's Torpedo should not read as the same shot, matching
// TntChargeVisual's own "bright emissive reads as live at a glance"
// convention.
glm::vec3 projectileEmissiveColor(ProjectileType type) {
    switch (type) {
        case ProjectileType::Rocket: return glm::vec3(1.0f, 0.35f, 0.05f);
        case ProjectileType::ShieldBolt: return glm::vec3(0.15f, 0.55f, 1.0f);
        case ProjectileType::RepairBeam: return glm::vec3(0.25f, 1.0f, 0.45f);
        case ProjectileType::RadarPing: return glm::vec3(1.0f, 0.85f, 0.15f);
        case ProjectileType::Torpedo: return glm::vec3(0.6f, 0.75f, 1.0f);
        case ProjectileType::Missile: return glm::vec3(1.0f, 0.2f, 0.2f);
    }
    return glm::vec3(1.0f, 0.35f, 0.05f);
}

// Real, minimal rotation aligning the box's own local +X (its long axis,
// see halfExtents below) with a target direction -- standard axis-angle
// from the cross product, with the real degenerate 180-degree case
// handled explicitly (cross product of anti-parallel vectors is zero).
glm::quat rotationAligningXAxisTo(glm::vec3 direction) {
    constexpr glm::vec3 kFromAxis(1.0f, 0.0f, 0.0f);
    float lengthSq = glm::dot(direction, direction);
    if (lengthSq < 1e-10f) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    glm::vec3 to = direction / std::sqrt(lengthSq);
    float d = glm::dot(kFromAxis, to);
    if (d > 0.9999f) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    if (d < -0.9999f) {
        glm::vec3 axis = glm::cross(kFromAxis, glm::vec3(0.0f, 1.0f, 0.0f));
        if (glm::dot(axis, axis) < 1e-10f) axis = glm::cross(kFromAxis, glm::vec3(0.0f, 0.0f, 1.0f));
        return glm::angleAxis(glm::pi<float>(), glm::normalize(axis));
    }
    glm::vec3 axis = glm::normalize(glm::cross(kFromAxis, to));
    return glm::angleAxis(std::acos(glm::clamp(d, -1.0f, 1.0f)), axis);
}

} // namespace

core::EntityId spawnProjectileVisual(core::ECS& ecs, core::MeshLibrary& meshLibrary,
                                       const core::ProceduralMaterialLibrary& materials, VmaAllocator allocator,
                                       VkDevice device, VkCommandPool cmdPool, VkQueue queue,
                                       const ProjectileState& projectile, const char* name) {
    glm::vec3 halfExtents(0.35f, 0.08f, 0.08f); // long axis along local +X, see rotationAligningXAxisTo()
    core::Mesh mesh = core::Mesh::createBox(allocator, device, cmdPool, queue, halfExtents);
    if (mesh.vertexBuffer() == VK_NULL_HANDLE) return core::kNullEntity;
    uint32_t meshHandle = meshLibrary.registerMesh(std::move(mesh));

    core::EntityId entity = ecs.createEntity(name);
    if (auto* transform = ecs.tryGetComponent<core::Transform>(entity)) {
        transform->position = projectile.position;
        transform->rotation = rotationAligningXAxisTo(projectile.velocity);
    }

    glm::vec3 color = projectileEmissiveColor(projectile.type);
    auto& renderable = ecs.addComponent<core::Renderable>(entity);
    renderable.meshHandle = meshHandle;
    renderable.baseColor = glm::vec4(color, 1.0f);
    renderable.metallic = 0.2f;
    renderable.roughness = 0.4f;
    renderable.albedoTexture = materials.metal.albedo;
    renderable.normalTexture = materials.metal.normal;
    renderable.metallicTexture = materials.metal.metallic;
    renderable.roughnessTexture = materials.metal.roughness;
    renderable.aoTexture = materials.metal.ao;
    renderable.emissiveColor = color;
    renderable.emissiveIntensity = 1.1f; // real, deliberately brighter than a TNT charge -- reads as a fast-moving shot

    auto& meshSource = ecs.addComponent<core::MeshSource>(entity);
    meshSource.kind = core::MeshSourceKind::Box;
    meshSource.params = halfExtents;

    return entity;
}

void updateProjectileVisualTransform(core::EntityId entity, core::ECS& ecs, const ProjectileState& projectile) {
    if (entity == core::kNullEntity) return;
    if (auto* transform = ecs.tryGetComponent<core::Transform>(entity)) {
        transform->position = projectile.position;
        transform->rotation = rotationAligningXAxisTo(projectile.velocity);
    }
}

} // namespace engine::tntwars
