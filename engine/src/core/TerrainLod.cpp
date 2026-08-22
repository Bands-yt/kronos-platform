#include "core/TerrainLod.hpp"

#include <algorithm>
#include <cmath>

namespace engine::core {

namespace {
constexpr float kEpsilon = 1e-6f;

// Iterative rather than recursive: a corrupted or hostile settings block
// must not be able to blow the stack, and the explicit stack also makes
// the node budget trivially enforceable mid-traversal.
struct PendingNode {
    glm::vec2 centerXZ;
    float halfSize;
    int level;
};
} // namespace

float cdlodLevelRange(const CdlodSettings& settings, int level) {
    const float base = std::max(settings.baseRangeMeters, kEpsilon);
    const int clampedLevel = std::clamp(level, 0, std::max(settings.levelCount - 1, 0));
    // Each level doubles the previous range.
    return base * static_cast<float>(1 << clampedLevel);
}

float cdlodMorphFactor(const CdlodSettings& settings, int level, float distanceMeters) {
    const float range = cdlodLevelRange(settings, level);
    const float fraction = std::clamp(settings.morphRegionFraction, 0.0f, 1.0f);
    if (fraction <= kEpsilon) return 0.0f; // morphing disabled

    // Morph across the outer `fraction` of this level's range.
    const float morphStart = range * (1.0f - fraction);
    const float morphEnd = range;
    if (distanceMeters <= morphStart) return 0.0f;
    if (distanceMeters >= morphEnd) return 1.0f;

    const float span = morphEnd - morphStart;
    if (span <= kEpsilon) return distanceMeters >= morphEnd ? 1.0f : 0.0f;
    const float t = (distanceMeters - morphStart) / span;
    // Smoothstep rather than linear: a linear ramp has a discontinuous
    // derivative at both ends, which reads as a visible "kick" exactly
    // when the geometry starts and stops moving.
    return t * t * (3.0f - 2.0f * t);
}

float distanceToNodeXZ(const glm::vec2& centerXZ, float halfSizeMeters, const glm::vec2& pointXZ) {
    const float half = std::max(halfSizeMeters, 0.0f);
    // Distance to an axis-aligned square: clamp the point into the box
    // and measure. Zero when the point is inside, which is correct --
    // the camera standing on a node is not "far" from it.
    const float dx = std::max(std::fabs(pointXZ.x - centerXZ.x) - half, 0.0f);
    const float dz = std::max(std::fabs(pointXZ.y - centerXZ.y) - half, 0.0f);
    return std::sqrt(dx * dx + dz * dz);
}

CdlodSelection selectCdlodNodes(const CdlodSettings& settings, const glm::vec2& terrainCenterXZ,
                                 float terrainHalfSizeMeters, const glm::vec3& cameraPosition) {
    CdlodSelection selection;

    const int levelCount = std::max(settings.levelCount, 1);
    const int rootLevel = levelCount - 1;
    if (terrainHalfSizeMeters <= kEpsilon) return selection;

    // A non-finite camera would make every comparison below false and
    // silently select nothing; treat it as the terrain centre instead so
    // the terrain still renders.
    glm::vec2 cameraXZ(cameraPosition.x, cameraPosition.z);
    if (!std::isfinite(cameraXZ.x) || !std::isfinite(cameraXZ.y)) cameraXZ = terrainCenterXZ;

    std::vector<PendingNode> stack;
    stack.push_back(PendingNode{terrainCenterXZ, terrainHalfSizeMeters, rootLevel});

    const int nodeLimit = std::max(settings.maxSelectedNodes, 1);

    while (!stack.empty()) {
        if (static_cast<int>(selection.nodes.size()) >= nodeLimit) {
            selection.hitNodeLimit = true;
            break;
        }

        const PendingNode node = stack.back();
        stack.pop_back();

        const float distance = distanceToNodeXZ(node.centerXZ, node.halfSize, cameraXZ);

        // Level 0 is the finest there is -- it can never subdivide, so it
        // is always emitted regardless of distance.
        if (node.level <= 0) {
            CdlodNode selected;
            selected.centerXZ = node.centerXZ;
            selected.halfSizeMeters = node.halfSize;
            selected.level = 0;
            selected.morphFactor = cdlodMorphFactor(settings, 0, distance);
            selection.nodes.push_back(selected);
            continue;
        }

        // Inside the range of the level BELOW this one means finer
        // geometry is warranted here: subdivide.
        const float childRange = cdlodLevelRange(settings, node.level - 1);
        if (distance < childRange) {
            const float childHalf = node.halfSize * 0.5f;
            // Degenerate subdivision guard: if halving underflows, emit
            // this node rather than pushing four zero-size children.
            if (childHalf <= kEpsilon) {
                CdlodNode selected;
                selected.centerXZ = node.centerXZ;
                selected.halfSizeMeters = node.halfSize;
                selected.level = node.level;
                selected.morphFactor = cdlodMorphFactor(settings, node.level, distance);
                selection.nodes.push_back(selected);
                continue;
            }
            for (int i = 0; i < 4; ++i) {
                const float offsetX = (i & 1) ? childHalf : -childHalf;
                const float offsetZ = (i & 2) ? childHalf : -childHalf;
                stack.push_back(PendingNode{node.centerXZ + glm::vec2(offsetX, offsetZ), childHalf, node.level - 1});
            }
            continue;
        }

        CdlodNode selected;
        selected.centerXZ = node.centerXZ;
        selected.halfSizeMeters = node.halfSize;
        selected.level = node.level;
        selected.morphFactor = cdlodMorphFactor(settings, node.level, distance);
        selection.nodes.push_back(selected);
    }

    return selection;
}

} // namespace engine::core
