#include "tntwars/DestructibleGeometry.hpp"

#include <algorithm>

namespace engine::tntwars {

std::vector<DestructibleSegment> buildSegmentedWall(glm::vec3 center, glm::vec3 wallHalfExtents, int columns,
                                                      int rows, float segmentHealth) {
    columns = std::max(columns, 1);
    rows = std::max(rows, 1);

    glm::vec3 segmentHalfExtents{wallHalfExtents.x / static_cast<float>(columns),
                                  wallHalfExtents.y / static_cast<float>(rows), wallHalfExtents.z};

    std::vector<DestructibleSegment> segments;
    segments.reserve(static_cast<size_t>(columns) * static_cast<size_t>(rows));
    for (int row = 0; row < rows; ++row) {
        float y = center.y - wallHalfExtents.y + segmentHalfExtents.y * (2.0f * static_cast<float>(row) + 1.0f);
        for (int col = 0; col < columns; ++col) {
            float x = center.x - wallHalfExtents.x + segmentHalfExtents.x * (2.0f * static_cast<float>(col) + 1.0f);
            DestructibleSegment segment;
            segment.position = glm::vec3(x, y, center.z);
            segment.halfExtents = segmentHalfExtents;
            segment.health = segmentHealth;
            segment.maxHealth = segmentHealth;
            segments.push_back(segment);
        }
    }
    return segments;
}

void applyDamageToSegment(DestructibleSegment& segment, float damage) {
    if (damage <= 0.0f) return;
    segment.health = std::max(0.0f, segment.health - damage);
}

bool isSegmentDestroyed(const DestructibleSegment& segment) { return segment.health <= 0.0f; }

size_t countAliveSegments(const std::vector<DestructibleSegment>& segments) {
    return static_cast<size_t>(
        std::count_if(segments.begin(), segments.end(), [](const DestructibleSegment& s) { return !isSegmentDestroyed(s); }));
}

bool isStructureFullyDestroyed(const std::vector<DestructibleSegment>& segments) {
    if (segments.empty()) return false;
    return countAliveSegments(segments) == 0;
}

} // namespace engine::tntwars
