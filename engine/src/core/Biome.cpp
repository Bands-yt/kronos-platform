#include "core/Biome.hpp"

namespace engine::core {

namespace {
constexpr BiomePreset kBiomePresets[kBiomeCount] = {
    {BiomeId::Forest, "Forest", glm::vec3(0.75f, 0.85f, 0.70f), 2.2f, glm::vec3(0.10f, 0.16f, 0.12f),
     glm::vec3(0.06f, 0.08f, 0.05f)},
    {BiomeId::Desert, "Desert", glm::vec3(1.0f, 0.92f, 0.75f), 4.2f, glm::vec3(0.22f, 0.20f, 0.16f),
     glm::vec3(0.18f, 0.14f, 0.09f)},
    {BiomeId::Cave, "Cave", glm::vec3(0.5f, 0.55f, 0.65f), 0.15f, glm::vec3(0.03f, 0.03f, 0.04f),
     glm::vec3(0.02f, 0.02f, 0.02f)},
};
} // namespace

const char* biomeName(BiomeId biome) { return kBiomePresets[static_cast<size_t>(biome)].name; }

const BiomePreset& biomePreset(BiomeId biome) { return kBiomePresets[static_cast<size_t>(biome)]; }

void applyBiomeLighting(BiomeId biome, SceneLighting& lighting) {
    const BiomePreset& preset = biomePreset(biome);
    lighting.color = preset.lightColor;
    lighting.intensity = preset.lightIntensity;
    lighting.ambient = preset.ambient;
    lighting.ambientGround = preset.ambientGround;
}

} // namespace engine::core
