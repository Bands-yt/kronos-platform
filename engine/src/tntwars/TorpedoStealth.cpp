#include "tntwars/TorpedoStealth.hpp"

#include "tntwars/MapLayout.hpp"

namespace engine::tntwars {

bool isDetectedBySonar(glm::vec3 torpedoPosition, const std::vector<glm::vec3>& sonarSources, float detectionRadius) {
    for (const glm::vec3& source : sonarSources) {
        glm::vec3 delta = torpedoPosition - source;
        float distanceSq = delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;
        if (distanceSq <= detectionRadius * detectionRadius) return true;
    }
    return false;
}

std::vector<glm::vec3> sonarSourcePositions(MapId map) {
    std::vector<glm::vec3> sources;
    for (const MapLayoutPiece& piece : buildMapLayout(map)) {
        if (piece.name.rfind("SonarBuoy", 0) == 0) sources.push_back(piece.position);
    }
    return sources;
}

} // namespace engine::tntwars
