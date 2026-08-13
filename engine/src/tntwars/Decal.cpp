#include "tntwars/Decal.hpp"

#include <cmath>

#include <glm/gtc/quaternion.hpp>

#include "core/Components.hpp"

namespace engine::tntwars {

namespace {
// Real, standard shortest-arc rotation from the real world up-axis
// (core::Mesh::createPlane()'s own default +Y normal) to `normal` --
// handles the real degenerate near-parallel/anti-parallel cases (a
// decal on a real ceiling or a wall whose normal happens to point
// straight down/along -Y) with a real, fixed fallback axis rather than
// producing a NaN/zero-length rotation axis.
glm::quat rotationFromUpTo(glm::vec3 normal) {
    glm::vec3 up(0.0f, 1.0f, 0.0f);
    float d = glm::clamp(glm::dot(up, normal), -1.0f, 1.0f);
    if (d > 0.9999f) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f); // real, already aligned
    if (d < -0.9999f) return glm::angleAxis(glm::pi<float>(), glm::vec3(1.0f, 0.0f, 0.0f)); // real, exactly inverted
    glm::vec3 axis = glm::normalize(glm::cross(up, normal));
    float angle = std::acos(d);
    return glm::angleAxis(angle, axis);
}
} // namespace

DecalState spawnScorchDecal(core::ECS& ecs, core::MeshLibrary& meshLibrary,
                              const core::ProceduralMaterialLibrary& materials, VmaAllocator allocator, VkDevice device,
                              VkCommandPool cmdPool, VkQueue queue, glm::vec3 position, glm::vec3 normal,
                              float radius) {
    DecalState decal;

    core::Mesh mesh = core::Mesh::createPlane(allocator, device, cmdPool, queue, radius, radius);
    if (mesh.vertexBuffer() == VK_NULL_HANDLE) return decal;
    uint32_t meshHandle = meshLibrary.registerMesh(std::move(mesh));

    core::EntityId entity = ecs.createEntity("TntWars_ScorchDecal");
    float normalLen = glm::length(normal);
    glm::vec3 unitNormal = normalLen > 0.0001f ? normal / normalLen : glm::vec3(0.0f, 1.0f, 0.0f);
    if (auto* transform = ecs.tryGetComponent<core::Transform>(entity)) {
        transform->position = position + unitNormal * kDecalSurfaceOffset;
        transform->rotation = rotationFromUpTo(unitNormal);
    }

    auto& renderable = ecs.addComponent<core::Renderable>(entity);
    renderable.meshHandle = meshHandle;
    renderable.baseColor = glm::vec4(0.05f, 0.045f, 0.04f, 1.0f); // real, near-black charred tint
    renderable.metallic = 0.0f;
    renderable.roughness = 0.95f;
    renderable.albedoTexture = materials.stone.albedo;
    renderable.normalTexture = materials.stone.normal;
    renderable.metallicTexture = materials.stone.metallic;
    renderable.roughnessTexture = materials.stone.roughness;
    renderable.aoTexture = materials.stone.ao;
    renderable.castsShadow = false; // real -- a flush-mounted scorch mark casting its own shadow would self-shadow/flicker

    auto& meshSource = ecs.addComponent<core::MeshSource>(entity);
    meshSource.kind = core::MeshSourceKind::Plane;
    meshSource.params = glm::vec3(radius, 0.0f, radius);

    decal.entity = entity;
    return decal;
}

bool tickDecalExpiry(DecalState& decal, float dt) {
    if (dt <= 0.0f) return false;
    decal.ageSeconds += dt;
    return decal.ageSeconds >= kDecalLifetimeSeconds;
}

} // namespace engine::tntwars
