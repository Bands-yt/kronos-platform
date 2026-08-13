#include "core/PhysicsMaterial.hpp"

#include <cmath>

#include <glm/gtc/constants.hpp>

namespace engine::core {

bool PhysicsMaterial::validate(std::string& outError) const {
    if (friction < 0.0f) {
        outError = "friction must be non-negative";
        return false;
    }
    if (restitution < 0.0f || restitution > 1.0f) {
        outError = "restitution must be in [0,1]";
        return false;
    }
    if (density < 0.0f) {
        outError = "density must be non-negative";
        return false;
    }
    return true;
}

const char* physicsMaterialPresetName(PhysicsMaterialPreset preset) {
    switch (preset) {
        case PhysicsMaterialPreset::Custom: return "Custom";
        case PhysicsMaterialPreset::Metal: return "Metal";
        case PhysicsMaterialPreset::Rubber: return "Rubber";
        case PhysicsMaterialPreset::Wood: return "Wood";
        case PhysicsMaterialPreset::Stone: return "Stone";
    }
    return "Custom";
}

bool physicsMaterialPresetFromName(const std::string& name, PhysicsMaterialPreset& out) {
    static constexpr PhysicsMaterialPreset kAll[] = {PhysicsMaterialPreset::Custom, PhysicsMaterialPreset::Metal,
                                                       PhysicsMaterialPreset::Rubber, PhysicsMaterialPreset::Wood,
                                                       PhysicsMaterialPreset::Stone};
    for (PhysicsMaterialPreset candidate : kAll) {
        if (name == physicsMaterialPresetName(candidate)) {
            out = candidate;
            return true;
        }
    }
    return false;
}

PhysicsMaterial physicsMaterialForPreset(PhysicsMaterialPreset preset) {
    switch (preset) {
        // Steel-on-steel friction (~0.4-0.6 dry), a modest bounce (metal
        // doesn't absorb much energy but isn't especially elastic either),
        // and steel's real density (~7850 kg/m^3).
        case PhysicsMaterialPreset::Metal: return PhysicsMaterial{0.4f, 0.2f, 7850.0f};
        // Rubber's friction coefficient is often measured *above* 1.0
        // against itself/asphalt; capped at 1.0 here as an engine-safe
        // ceiling rather than an exotic outlier value. Real rubber bounces
        // a lot (a basketball's restitution is ~0.7-0.8), and rubber's
        // density is close to water's (~1.1-1.5 g/cm^3 depending on
        // compound; 1100 kg/m^3 as a representative middle).
        case PhysicsMaterialPreset::Rubber: return PhysicsMaterial{1.0f, 0.8f, 1100.0f};
        // Everyday wood-on-wood friction, a small bounce (wood is fairly
        // rigid but does return some energy, unlike stone), and a real
        // density in the middle of common lumber (pine ~500, oak ~700
        // kg/m^3).
        case PhysicsMaterialPreset::Wood: return PhysicsMaterial{0.5f, 0.3f, 600.0f};
        // Rough-surface friction, very little bounce (stone mostly
        // absorbs/scatters impact energy rather than returning it), and
        // granite's real density (~2700 kg/m^3).
        case PhysicsMaterialPreset::Stone: return PhysicsMaterial{0.7f, 0.1f, 2700.0f};
        case PhysicsMaterialPreset::Custom: break;
    }
    return PhysicsMaterial{};
}

float boxVolume(glm::vec3 halfExtents) { return 8.0f * halfExtents.x * halfExtents.y * halfExtents.z; }

float sphereVolume(float radius) { return (4.0f / 3.0f) * glm::pi<float>() * radius * radius * radius; }

float capsuleVolume(float radius, float halfHeight) {
    float cylinder = glm::pi<float>() * radius * radius * (2.0f * halfHeight);
    float sphere = sphereVolume(radius); // the two hemispherical caps together form one full sphere
    return cylinder + sphere;
}

float meshVolume(const std::vector<glm::vec3>& positions, const std::vector<uint32_t>& indices) {
    double signedVolume = 0.0;
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        const glm::vec3& v0 = positions[indices[i]];
        const glm::vec3& v1 = positions[indices[i + 1]];
        const glm::vec3& v2 = positions[indices[i + 2]];
        signedVolume += static_cast<double>(glm::dot(v0, glm::cross(v1, v2))) / 6.0;
    }
    return static_cast<float>(std::fabs(signedVolume));
}

} // namespace engine::core
