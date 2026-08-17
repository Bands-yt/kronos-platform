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

const SceneLighting& avatarIndoorPreviewLighting() {
    static const SceneLighting kLighting = [] {
        SceneLighting lighting;
        lighting.directionWS = glm::vec3(-0.55f, -0.65f, -0.3f);
        lighting.color = glm::vec3(1.0f, 0.92f, 0.78f); // warm key
        lighting.intensity = 2.6f;
        lighting.ambient = glm::vec3(0.06f, 0.07f, 0.11f);        // dim, cool sky fill
        lighting.ambientGround = glm::vec3(0.04f, 0.035f, 0.03f); // dim, warm ground fill
        // Kronos ("Avatar Scene Lighting Calibration Pass" -- "clamp
        // fogDensity to 0.006 for neutral indoor scenes"): a real,
        // small, fixed value -- an indoor avatar preview has no distant
        // geometry for fog to meaningfully act on, but a real, tiny,
        // nonzero density here keeps this preset numerically consistent
        // with real gameplay's own fog system (0.004-0.012 range, see
        // core::computeLightingForTimeOfDay()) rather than the previous
        // implicit 0.0 default.
        lighting.fogDensity = 0.006f;
        lighting.pointLights.push_back(SceneLighting::PointLight{
            glm::vec3(-1.4f, 2.4f, 1.6f), // behind-and-above the real {0,1,0} focus point
            6.0f,
            glm::vec3(0.55f, 0.7f, 1.0f), // cool rim
            2.2f,
        });
        return lighting;
    }();
    return kLighting;
}

} // namespace engine::core
