#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

namespace engine::core {

// CDLOD (Continuous Distance-Dependent Level of Detail) node selection.
//
// This header is deliberately pure CPU maths with no Vulkan, no ECS and
// no Terrain dependency: selection is the part that decides *what* to
// draw and at *which* detail, and keeping it separable is what makes it
// unit-testable at all. The renderer consumes the selected node list.
//
// The central idea: the terrain is a quadtree. Starting at the root, a
// node is subdivided if the camera is inside its "LOD range"; otherwise
// it is drawn at its own level. Each range is roughly double the last, so
// detail falls off with distance the way perspective already does.
//
// Popping is solved by MORPHING, not by hiding the transition. Every
// node reports a morph factor in [0,1] that says how far it has
// blended toward its parent's coarser geometry. The vertex shader lerps
// each vertex toward the position it would have at the parent level, so
// by the time a node is actually swapped out its geometry is already
// identical to what replaces it and the swap is invisible.

struct CdlodSettings {
    // Detail levels. Level 0 is the finest; `levelCount - 1` is the root.
    int levelCount = 5;
    // World-space distance at which the FINEST level stops being used.
    // Every coarser level doubles this.
    float baseRangeMeters = 64.0f;
    // Fraction of a level's range over which morphing happens, measured
    // from the far edge inward. 0.25 means the outermost quarter of each
    // range is a blend zone.
    //
    // Too small and the morph is a visible rush; too large and the
    // finest geometry is never seen at full detail. A quarter is the
    // usual compromise.
    float morphRegionFraction = 0.25f;
    // Guard against a pathological camera (inside the terrain, NaN
    // position) subdividing the whole tree.
    int maxSelectedNodes = 4096;
};

struct CdlodNode {
    // Node centre and half-extent on the XZ plane, in world units.
    glm::vec2 centerXZ{0.0f};
    float halfSizeMeters = 0.0f;
    // 0 = finest.
    int level = 0;
    // 0 = fully this level's geometry, 1 = fully morphed to the parent's.
    // The vertex shader uses this directly.
    float morphFactor = 0.0f;
};

struct CdlodSelection {
    std::vector<CdlodNode> nodes;
    // True when selection stopped early at maxSelectedNodes. Surfaced so
    // a caller can tell "the terrain really is this simple" apart from
    // "we ran out of budget", which look identical in the node list.
    bool hitNodeLimit = false;
};

// World-space distance at which `level` stops being used.
[[nodiscard]] float cdlodLevelRange(const CdlodSettings& settings, int level);

// Morph factor for a node at `level` whose nearest point is
// `distanceMeters` from the camera.
//
// Returns 0 well inside the level's range and ramps to 1 at its far
// edge. Being continuous in distance is the whole point: any jump here
// becomes a visible pop on screen.
[[nodiscard]] float cdlodMorphFactor(const CdlodSettings& settings, int level, float distanceMeters);

// Selects the visible node set for a camera at `cameraPosition`.
//
// `terrainCenterXZ` / `terrainHalfSizeMeters` describe the root node.
// Nodes are returned finest-first within each subtree; order is not
// otherwise meaningful.
[[nodiscard]] CdlodSelection selectCdlodNodes(const CdlodSettings& settings, const glm::vec2& terrainCenterXZ,
                                               float terrainHalfSizeMeters, const glm::vec3& cameraPosition);

// Shortest distance from `point` to the node's XZ square (0 when inside).
// Using the nearest point rather than the centre is what stops a large
// node the camera is standing on from being treated as distant.
[[nodiscard]] float distanceToNodeXZ(const glm::vec2& centerXZ, float halfSizeMeters, const glm::vec2& pointXZ);

} // namespace engine::core
