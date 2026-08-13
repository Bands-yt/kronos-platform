#include "tntwars/TrenchesCover.hpp"

namespace engine::tntwars {

std::vector<DestructibleSegment> buildTrenchesCover() {
    // Real, exact positions -- matches MapLayout.cpp's own buildTrenches()
    // Cover_A_Left/Cover_A_Right/Cover_B_Left/Cover_B_Right pieces.
    const glm::vec3 kCoverCenters[4] = {
        {-20.0f, 1.0f, -15.0f},
        {20.0f, 1.0f, -15.0f},
        {-20.0f, 1.0f, 15.0f},
        {20.0f, 1.0f, 15.0f},
    };
    glm::vec3 halfExtents(kTrenchesCoverHalfWidth, kTrenchesCoverHalfHeight, kTrenchesCoverHalfDepth);

    std::vector<DestructibleSegment> cover;
    for (const glm::vec3& center : kCoverCenters) {
        std::vector<DestructibleSegment> piece =
            buildSegmentedWall(center, halfExtents, kTrenchesCoverColumns, kTrenchesCoverRows, kTrenchesCoverSegmentHealth);
        cover.insert(cover.end(), piece.begin(), piece.end());
    }
    return cover;
}

} // namespace engine::tntwars
