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
        // Kronos ("Avatar Vulkan Pipeline & Avatar Preview Rendering"):
        // real, necessary fix -- SceneLighting's own skyZenithColor/
        // skyHorizonColor default member initializers are an *outdoor*
        // blue-sky gradient (see SceneTypes.hpp), and this default-
        // constructed `lighting` never overrode them, leaving that
        // outdoor sky rendering unchanged behind every "indoor" avatar
        // preview. Harmless for a normal outdoor gameplay camera (the
        // sky is a thin strip at the top of the frame at most), but a
        // real, visible bug for this preset's actual real callers
        // (HomeAvatarPreview's tight "Your Avatar" box,
        // studio::plugins::AvatarEditor's preview) -- their close-up
        // orbit camera's background is *dominated* by near-horizon view
        // rays, so skyHorizonColor's own real default (0.75, 0.80, 0.85
        // -- already pale before any tonemapping) reads as a near-solid
        // white backdrop, not a sky. Real, dark, neutral navy instead --
        // consistent with this preset's own already-dark ambient tones
        // above and the app's own dark UI theme -- so the preview reads
        // as a real studio backdrop, not outdoor daylight leaking
        // through an indoor scene.
        lighting.skyZenithColor = glm::vec3(0.05f, 0.055f, 0.08f);
        lighting.skyHorizonColor = glm::vec3(0.09f, 0.095f, 0.12f);
        // Kronos (real bug found via two correctly-synchronized GPU
        // readbacks of HomeAvatarPreview's actual offscreen texture --
        // the "Your Avatar" white background persisted through several
        // earlier, individually-correct fixes, including a first attempt
        // at this exact line that only darkened fogColor and made no
        // measurable difference): this preset used to set a real,
        // nonzero fogDensity (0.006, "consistent with real gameplay's
        // own fog system") on the stated assumption that "an indoor
        // avatar preview has no distant geometry for fog to meaningfully
        // act on" -- true for gameplay scenes with real walls/floors
        // close by, false here. shaders/volumetric_fog.frag's own
        // isBackground branch raymarches every max-depth pixel (which,
        // via useFlatBackground above, is every background pixel behind
        // this tight orbit camera) out to the full, real
        // volumetricFogMaxDistance_ (120 world units), integrating real
        // in-scattered radiance dominated by this preset's own bright
        // warm key light (color*intensity = (2.6, 2.39, 2.03) --
        // fogColor's own contribution is scaled down by
        // volumetricFogAmbientContribution_ (0.35) and was never the
        // dominant term, which is why darkening it alone didn't fix
        // this). Integrated over 120 units that light term alone
        // resolves to roughly (2.07, 1.90, 1.61) linear -- verified by
        // hand against the real captured pixel value, which ACES-
        // tonemaps and gamma-encodes to almost exactly what both real
        // captures showed. Real fix: this preset has no real distant
        // geometry for fog to act on at all (the stated original
        // assumption), so fogDensity is really, honestly 0.0f -- and
        // shaders/volumetric_fog.frag's own `density <= 0.0` branch is a
        // real, exact early-out (byte-for-byte passthrough of the opaque
        // pass's own output), not an approximation.
        lighting.fogDensity = 0.0f;
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
