#include "core/SceneTypes.hpp"

#include <cstddef>

namespace engine::core {

VkVertexInputBindingDescription InstanceData::bindingDescription() {
    VkVertexInputBindingDescription binding{};
    binding.binding = 1; // binding 0 is Vertex (per-vertex rate) -- see Mesh.hpp
    binding.stride = sizeof(InstanceData);
    binding.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
    return binding;
}

std::vector<VkVertexInputAttributeDescription> InstanceData::attributeDescriptions() {
    // Locations 0/1/2/3 are Vertex's position/normal/uv/tangent (binding
    // 0) -- tangent (location 3, see Mesh.hpp) is what pushed this
    // struct's own locations up from 3-9 to 4-10. glm::mat4 consumes 4
    // consecutive locations (one per column, GLSL has no single attribute
    // format wide enough for a whole mat4) -- 4/5/6/7.
    // baseColor/metallicRoughness/emissive follow at 8/9/10.
    std::vector<VkVertexInputAttributeDescription> attrs(7);
    attrs[0] = {4, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(InstanceData, model) + 0 * sizeof(glm::vec4)};
    attrs[1] = {5, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(InstanceData, model) + 1 * sizeof(glm::vec4)};
    attrs[2] = {6, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(InstanceData, model) + 2 * sizeof(glm::vec4)};
    attrs[3] = {7, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(InstanceData, model) + 3 * sizeof(glm::vec4)};
    attrs[4] = {8, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(InstanceData, baseColor)};
    attrs[5] = {9, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(InstanceData, metallicRoughness)};
    attrs[6] = {10, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(InstanceData, emissive)};
    return attrs;
}

VkVertexInputBindingDescription ParticleInstanceData::bindingDescription() {
    VkVertexInputBindingDescription binding{};
    binding.binding = 1; // binding 0 is Vertex (the shared quad) -- see Mesh.hpp
    binding.stride = sizeof(ParticleInstanceData);
    binding.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
    return binding;
}

std::vector<VkVertexInputAttributeDescription> ParticleInstanceData::attributeDescriptions() {
    // Locations 0/1/2/3 are the shared quad's Vertex position/normal/uv/
    // tangent (binding 0) -- tangent (location 3, unused by particle.vert
    // but still present in the shared Vertex layout, see Mesh.hpp) is what
    // pushed this struct's own locations up from 3/4 to 4/5. Particle
    // data follows at 4/5.
    std::vector<VkVertexInputAttributeDescription> attrs(2);
    attrs[0] = {4, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(ParticleInstanceData, positionSize)};
    attrs[1] = {5, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(ParticleInstanceData, color)};
    return attrs;
}

} // namespace engine::core
