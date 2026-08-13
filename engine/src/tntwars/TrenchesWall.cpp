#include "tntwars/TrenchesWall.hpp"

namespace engine::tntwars {

std::vector<DestructibleSegment> buildTrenchesWall() {
    return buildSegmentedWall(glm::vec3(0.0f, kTrenchesWallHalfHeight, 0.0f),
                               glm::vec3(kTrenchesWallHalfWidth, kTrenchesWallHalfHeight, kTrenchesWallThickness),
                               kTrenchesWallColumns, kTrenchesWallRows, kTrenchesWallSegmentHealth);
}

bool isTrenchesWallBreached(const std::vector<DestructibleSegment>& wall) { return isStructureFullyDestroyed(wall); }

} // namespace engine::tntwars
