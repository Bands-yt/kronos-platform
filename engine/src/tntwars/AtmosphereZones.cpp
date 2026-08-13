#include "tntwars/AtmosphereZones.hpp"

#include <algorithm>

namespace engine::tntwars {

namespace {
float smoothstep(float edge0, float edge1, float x) {
    float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float zoneWeight(const AtmosphereZone& zone, glm::vec3 position) {
    float distance = glm::length(position - zone.position);
    if (distance >= zone.radius) return 0.0f;
    float innerEdge = zone.radius * 0.7f;
    if (distance <= innerEdge) return 1.0f;
    return 1.0f - smoothstep(innerEdge, zone.radius, distance);
}
} // namespace

AtmosphereSample sampleAtmosphereZones(const std::vector<AtmosphereZone>& zones, glm::vec3 position,
                                        const AtmosphereSample& defaultSample) {
    const AtmosphereZone* strongest = nullptr;
    float strongestWeight = 0.0f;
    for (const AtmosphereZone& zone : zones) {
        float weight = zoneWeight(zone, position);
        if (weight > strongestWeight) {
            strongestWeight = weight;
            strongest = &zone;
        }
    }

    if (strongest == nullptr || strongestWeight <= 0.0f) return defaultSample;

    AtmosphereSample result;
    result.fogColor = glm::mix(defaultSample.fogColor, strongest->fogColor, strongestWeight);
    result.fogDensity = glm::mix(defaultSample.fogDensity, strongest->fogDensity, strongestWeight);
    result.exposure = glm::mix(defaultSample.exposure, strongest->exposure, strongestWeight);
    return result;
}

std::vector<AtmosphereZone> buildSkyMapAtmosphereZones(glm::vec3 teamABaseCenter, glm::vec3 teamBBaseCenter) {
    std::vector<AtmosphereZone> zones;

    AtmosphereZone teamA;
    teamA.position = teamABaseCenter;
    teamA.radius = 45.0f;
    teamA.fogColor = glm::vec3(0.85f, 0.68f, 0.48f); // real, warm forge-lit haze
    teamA.fogDensity = 0.0022f;                       // real, slightly clearer than the open-sky baseline
    teamA.exposure = 1.05f;
    zones.push_back(teamA);

    AtmosphereZone teamB = teamA;
    teamB.position = teamBBaseCenter;
    zones.push_back(teamB);

    return zones;
}

std::vector<AtmosphereZone> buildSpaceMapAtmosphereZones(const std::vector<SpacePlatform>& platforms) {
    std::vector<AtmosphereZone> zones;

    for (const SpacePlatform& platform : platforms) {
        if (platform.type != SpacePlatformType::DerelictStation) continue;
        AtmosphereZone zone;
        zone.position = platform.center;
        zone.radius = platform.radius * 1.8f;
        zone.fogColor = glm::vec3(0.55f, 0.6f, 0.68f); // real, warm panel-lit interior haze
        zone.fogDensity = 0.001f;                       // real, clearer -- pressurized interior air
        zone.exposure = platform.major ? 1.15f : 1.08f;
        zones.push_back(zone);
    }

    return zones;
}

} // namespace engine::tntwars
